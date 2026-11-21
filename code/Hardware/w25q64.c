#include "w25q64.h"
#include "board_config.h"
#include "spi_bus.h"
#include <string.h>

/* PY25Q64HA V1.6：P11最大tPP=2.4ms/tSE=150ms（I/K，2.7..3.6V），
 * P29/32保护位、P65擦除、P69页编程、P80身份。软件时限20/250ms
 * 另含5ms调度裕量。禁止整片擦除、修改保护配置、OTP及自动解锁。 */
enum { PROGRAM_DEADLINE_MS=20U, ERASE_DEADLINE_MS=250U, PAGE_SIZE=256U };
typedef enum { OP_IDLE, OP_SR0, OP_SR1, OP_CONFIG, OP_ID, OP_EXISTING,
    OP_ENABLE, OP_WEL, OP_COMMAND, OP_POLL, OP_RESULT, OP_VERIFY, OP_DISABLE } op_phase_t;
static w25q64_state_t state = {.status=WATCH_NOT_READY, .operation_status=WATCH_NOT_READY};
static op_phase_t op;
static uint32_t address, started_ms, verified, total, completed, current;
static uint8_t pending[PAGE_SIZE];
static bool erasing, test_waiting, recovering, disabled;
static const uint8_t cross_pattern[6]={0xA5U,0U,0x5AU,0xC3U,0x3CU,0x81U};
_Static_assert(WATCH_EXT_FLASH_SIZE==0x800000UL,"Flash design size mismatch");
_Static_assert(SPI_BUS_MAX_TRANSFER_SIZE>=PAGE_SIZE,"SPI segment must hold a page");

static bool range_valid(uint32_t offset, size_t size)
{
    return size!=0U && offset<WATCH_EXT_FLASH_SIZE && size<=WATCH_EXT_FLASH_SIZE-offset;
}
static bool profile_ready(void)
{
    return state.raw_valid && state.raw_jedec_id==0x852017UL && state.stable_samples>=2U;
}
static watch_status_t status_read(uint8_t command, uint8_t *value)
{
    const uint8_t tx[2]={command,0xFFU};uint8_t rx[2]={0};
    watch_status_t s=spi_bus_transfer(tx,rx,sizeof(tx));
    if (s==WATCH_OK) {
        *value=rx[1];
        if (command==0x05U) state.write_enabled=(rx[1]&2U)!=0U;
    }
    return s;
}
static watch_status_t memory_transaction(uint8_t command,uint32_t offset,
                                        const uint8_t *tx,uint8_t *rx,size_t size)
{
    const uint8_t header[4]={command,(uint8_t)(offset>>16),(uint8_t)(offset>>8),(uint8_t)offset};
    const spi_bus_segment_t parts[2]={{header,NULL,sizeof(header)},{tx,rx,size}};
    return spi_bus_transaction(parts,size==0U ? 1U : 2U);
}
watch_status_t w25q64_identify(uint32_t *id)
{
    if (id==NULL) return WATCH_INVALID_ARGUMENT;
    *id=0U;
    if (state.busy) return WATCH_NOT_READY;
    const uint8_t tx[4]={0x9FU,0xFFU,0xFFU,0xFFU};uint8_t rx[4]={0};
    watch_status_t s=spi_bus_transfer(tx,rx,sizeof(tx));
    if (s!=WATCH_OK) return s;
    *id=((uint32_t)rx[1]<<16)|((uint32_t)rx[2]<<8)|rx[3];
    return *id==0U || *id==0xFFFFFFUL ? WATCH_INVALID_DATA : WATCH_OK;
}
watch_status_t w25q64_probe(uint32_t now_ms)
{
    if (state.busy) return WATCH_NOT_READY;
    uint32_t id=0U;watch_status_t s=w25q64_identify(&id);
    if (s==WATCH_OK) {
        if (state.raw_valid && state.raw_jedec_id==id) {
            if (state.stable_samples<UINT32_MAX) state.stable_samples++;
        } else state.stable_samples=1U;
    } else state.stable_samples=0U;
    /* 身份异常撤销写入归属；dirty保留，不猜测残留或自动清理。 */
    if (id!=0x852017UL || s!=WATCH_OK) state.owned=false;
    state.raw_jedec_id=id;state.raw_valid=s==WATCH_OK;state.sample_ms=now_ms;state.status=s;
    state.model=id==0x852017UL && s==WATCH_OK ? W25Q64_MODEL_PY25Q64HA_ID : W25Q64_MODEL_UNCONFIRMED;
    state.capacity_bytes=state.model==W25Q64_MODEL_PY25Q64HA_ID ? WATCH_EXT_FLASH_SIZE : 0U;
    state.capacity_confirmed=false; /* ID及手册映射，不冒充整片容量实测。 */
    return s;
}
watch_status_t w25q64_get_state(w25q64_state_t *result)
{
    if (result==NULL) return WATCH_INVALID_ARGUMENT;
    *result=state;return WATCH_OK;
}
watch_status_t w25q64_read(uint32_t offset,uint8_t *data,size_t size)
{
    if (data==NULL || size>PAGE_SIZE || !range_valid(offset,size)) return WATCH_INVALID_ARGUMENT;
    if (!profile_ready() || state.busy) return WATCH_NOT_READY;
    uint8_t sr=0U;watch_status_t s=status_read(0x05U,&sr);
    if (s!=WATCH_OK) return s;
    if ((sr&1U)!=0U) return WATCH_NOT_READY;
    s=status_read(0x35U,&state.sr1);
    if (s!=WATCH_OK) return s;
    if ((state.sr1&0x80U)!=0U) return WATCH_NOT_READY;
    return memory_transaction(0x03U,offset,NULL,data,size);
}
static watch_status_t enqueue(uint32_t offset,const uint8_t *data,size_t size,bool erase)
{
    if (!profile_ready() || state.busy || !state.owned) return WATCH_NOT_READY;
    if (offset<W25Q64_TEST_BASE || offset>=WATCH_EXT_FLASH_SIZE ||
        size>WATCH_EXT_FLASH_SIZE-offset) return WATCH_INVALID_ARGUMENT;
    address=offset;total=(uint32_t)size;completed=0U;verified=0U;erasing=erase;
    recovering=state.test_stage==FLASH_TEST_CLEANING;
    disabled=false;
    if (!erase) memcpy(pending,data,size);
    op=OP_SR0;state.busy=true;state.operation_status=WATCH_NOT_READY;
    return WATCH_OK;
}
watch_status_t w25q64_write(uint32_t offset,const uint8_t *data,size_t size)
{
    if (data==NULL || size>PAGE_SIZE || !range_valid(offset,size)) return WATCH_INVALID_ARGUMENT;
    return enqueue(offset,data,size,false);
}
watch_status_t w25q64_erase_sector(uint32_t offset)
{
    if (offset%W25Q64_TEST_SIZE!=0U || !range_valid(offset,W25Q64_TEST_SIZE)) return WATCH_INVALID_ARGUMENT;
    return enqueue(offset,NULL,W25Q64_TEST_SIZE,true);
}
static watch_status_t finish(watch_status_t result)
{
    state.busy=false;state.operation_status=result;op=OP_IDLE;
    return result;
}
static watch_status_t operation_process(uint32_t now_ms)
{
    uint8_t data[PAGE_SIZE];uint8_t command;watch_status_t s=WATCH_OK;
    switch (op) {
    case OP_SR0:
        s=status_read(0x05U,&state.sr0);
        if (s==WATCH_OK) {
            if ((state.sr0&0x7DU)!=0U) return finish(WATCH_NOT_READY);
            if ((state.sr0&2U)!=0U) {
                if (!recovering || disabled) return finish(WATCH_NOT_READY);
                op=OP_DISABLE;
            } else op=OP_SR1;
        }
        break;
    case OP_DISABLE:
        command=0x04U;s=spi_bus_transfer(&command,NULL,1U);
        if (s==WATCH_OK) {disabled=true;op=OP_SR0;}
        break;
    case OP_SR1:
        s=status_read(0x35U,&state.sr1);
        if (s==WATCH_OK) {
            if ((state.sr1&0xC0U)!=0U || ((state.sr1&4U)!=0U && !recovering)) return finish(WATCH_INVALID_DATA);
            op=OP_CONFIG;
        }
        break;
    case OP_CONFIG:
        s=status_read(0x15U,&state.config);
        if (s==WATCH_OK) {
            if ((state.config&4U)!=0U) return finish(WATCH_NOT_READY);
            op=OP_ID;
        }
        break;
    case OP_ID: {
        const uint8_t tx[4]={0x9FU,0xFFU,0xFFU,0xFFU};uint8_t rx[4]={0};
        s=spi_bus_transfer(tx,rx,4U);
        if (s==WATCH_OK) {
            if (rx[1]!=0x85U || rx[2]!=0x20U || rx[3]!=0x17U) {
                state.owned=false;return finish(WATCH_INVALID_DATA);
            }
            current=erasing ? 0U : PAGE_SIZE-((address+completed)%PAGE_SIZE);
            if (current>total-completed) current=total-completed;
            op=erasing ? OP_ENABLE : OP_EXISTING;
        }
        break;
    }
    case OP_EXISTING:
        s=memory_transaction(0x03U,address+completed,NULL,data,current);
        if (s==WATCH_OK) {
            for (uint32_t i=0U;i<current;++i) {
                if (data[i]!=0xFFU) return finish(WATCH_INVALID_DATA);
            }
            op=OP_ENABLE;
        }
        break;
    case OP_ENABLE:
        command=0x06U;s=spi_bus_transfer(&command,NULL,1U);
        state.dirty=true; /* 失败可能已被器件接受，不假定没有副作用。 */
        if (s==WATCH_OK) op=OP_WEL;
        break;
    case OP_WEL:
        s=status_read(0x05U,&state.sr0);
        if (s==WATCH_OK) {
            state.write_enabled=(state.sr0&2U)!=0U;
            if ((state.sr0&0x7FU)!=2U) return finish(WATCH_IO_ERROR);
            op=OP_COMMAND;
        }
        break;
    case OP_COMMAND:
        s=memory_transaction(erasing ? 0x20U : 0x02U,address+completed,
                             erasing ? NULL : pending+completed,NULL,current);
        started_ms=now_ms;
        if (s==WATCH_OK) op=OP_POLL;
        break;
    case OP_POLL:
        s=status_read(0x05U,&state.sr0);
        if (s==WATCH_OK) {
            if ((state.sr0&1U)==0U) {
                if ((state.sr0&2U)!=0U) return finish(WATCH_IO_ERROR);
                op=OP_RESULT;
            } else if ((uint32_t)(now_ms-started_ms)>=(erasing ? ERASE_DEADLINE_MS : PROGRAM_DEADLINE_MS)) {
                return finish(WATCH_TIMEOUT);
            }
        }
        break;
    case OP_RESULT:
        s=status_read(0x35U,&state.sr1);
        if (s==WATCH_OK) {
            if ((state.sr1&0x84U)!=0U) return finish(WATCH_IO_ERROR);
            verified=0U;op=OP_VERIFY;
        }
        break;
    case OP_VERIFY: {
        uint32_t size=erasing ? W25Q64_TEST_SIZE-verified : current;
        if (size>PAGE_SIZE) size=PAGE_SIZE;
        s=memory_transaction(0x03U,address+completed+verified,NULL,data,size);
        if (s==WATCH_OK) {
            for (uint32_t i=0U;i<size;++i) {
                if (data[i]!=(erasing ? 0xFFU : pending[completed+i])) return finish(WATCH_IO_ERROR);
            }
            verified+=size;
            if (!erasing || verified==W25Q64_TEST_SIZE) {
                if (erasing) {state.dirty=false;return finish(WATCH_OK);}
                completed+=current;
                if (completed==total) return finish(WATCH_OK);
                op=OP_SR0;
            }
        }
        break;
    }
    default:return finish(WATCH_IO_ERROR);
    }
    return s==WATCH_OK ? WATCH_NOT_READY : finish(s);
}
watch_status_t w25q64_test_start(void)
{
    if (!profile_ready() || state.busy || state.dirty ||
        (state.test_stage>=FLASH_TEST_SCAN && state.test_stage<=FLASH_TEST_ERASE)) return WATCH_NOT_READY;
    state.owned=false;state.test_progress=0U;state.test_status=WATCH_NOT_READY;
    state.failed_stage=FLASH_TEST_IDLE;
    state.test_stage=FLASH_TEST_SCAN;test_waiting=false;return WATCH_OK;
}
watch_status_t w25q64_test_recover(void)
{
    if (!state.owned || !state.dirty || !profile_ready() || state.busy ||
        state.test_stage!=FLASH_TEST_FAILED) return WATCH_NOT_READY;
    state.test_stage=FLASH_TEST_CLEANING;state.test_status=WATCH_NOT_READY;test_waiting=false;
    return WATCH_OK;
}
static watch_status_t test_fail(watch_status_t s)
{
    state.failed_stage=state.test_stage;
    state.test_stage=FLASH_TEST_FAILED;state.test_status=s;test_waiting=false;return s;
}
watch_status_t w25q64_process(uint32_t now_ms)
{
    if (state.busy) return operation_process(now_ms);
    if (test_waiting) {
        test_waiting=false;
        if (state.operation_status!=WATCH_OK) return test_fail(state.operation_status);
        if (state.test_stage==FLASH_TEST_CLEANING) state.test_stage=FLASH_TEST_CLEAN;
        else state.test_stage=(flash_test_stage_t)((unsigned)state.test_stage+1U);
        if (state.test_stage==FLASH_TEST_PASS || state.test_stage==FLASH_TEST_CLEAN) {
            state.test_status=WATCH_OK;state.owned=false;
        }
        return WATCH_OK;
    }
    uint8_t data[PAGE_SIZE];watch_status_t s=WATCH_OK;
    switch (state.test_stage) {
    case FLASH_TEST_SCAN:
        s=w25q64_read(W25Q64_TEST_BASE+state.test_progress,data,sizeof(data));
        if (s!=WATCH_OK) return test_fail(s);
        for (size_t i=0U;i<sizeof(data);++i) {
            if (data[i]!=0xFFU) {
                state.test_stage=FLASH_TEST_NONEMPTY;state.test_status=WATCH_INVALID_DATA;
                return WATCH_INVALID_DATA;
            }
        }
        state.test_progress+=sizeof(data);
        if (state.test_progress==W25Q64_TEST_SIZE) {
            if (w25q64_read(WATCH_EXT_FLASH_SIZE-1U,data,2U)!=WATCH_INVALID_ARGUMENT ||
                w25q64_write(WATCH_EXT_FLASH_SIZE,data,1U)!=WATCH_INVALID_ARGUMENT ||
                w25q64_erase_sector(W25Q64_TEST_BASE+1U)!=WATCH_INVALID_ARGUMENT) return test_fail(WATCH_IO_ERROR);
            state.owned=true;state.test_stage=FLASH_TEST_FULL;
        }
        return WATCH_OK;
    case FLASH_TEST_FULL:
        for (unsigned i=0U;i<PAGE_SIZE;++i) data[i]=(uint8_t)(i^0xA5U);
        s=w25q64_write(W25Q64_TEST_BASE,data,sizeof(data));break;
    case FLASH_TEST_CROSS:s=w25q64_write(W25Q64_TEST_BASE+510U,cross_pattern,sizeof(cross_pattern));break;
    case FLASH_TEST_LAST:data[0]=0x69U;s=w25q64_write(WATCH_EXT_FLASH_SIZE-1U,data,1U);break;
    case FLASH_TEST_ERASE:
    case FLASH_TEST_CLEANING:s=w25q64_erase_sector(W25Q64_TEST_BASE);break;
    default:return state.test_status;
    }
    if (s!=WATCH_OK) return test_fail(s);
    test_waiting=true;return WATCH_NOT_READY;
}
