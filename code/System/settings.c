#include "settings.h"
#include "board_config.h"
#include "w25q64.h"
#include "crc32.h"
#include <string.h>

/* 小端格式：magic[4]、格式[1]、参数版本[1]、长度[2]、代号[4]、
 * CRC[4]、最后单独写入的提交标记[4]、最多128字节载荷。
 * CRC覆盖前12字节和实际载荷，不含CRC自身及提交标记。 */
enum { HEADER=20U, FRAME=HEADER+SETTINGS_MAX_PAYLOAD, MAGIC=0xA55AF072U,
       COMMIT=0x434F4D54U };
_Static_assert(FRAME<=256U,"Settings record must fit one bounded read");
typedef enum { SLOT_UNKNOWN, SLOT_EMPTY, SLOT_VALID, SLOT_INCOMPLETE } slot_kind_t;
typedef struct {
    slot_kind_t kind;
    uint32_t generation;
    uint16_t length;
    bool blank, tail_blank;
    uint8_t frame[FRAME];
} slot_t;
typedef enum { IDLE, CLAIM_LOAD, SCAN, SELECT, ERASE,
    WAIT_ERASE, WRITE_BODY, WAIT_BODY, WRITE_COMMIT, WAIT_COMMIT, VERIFY } phase_t;
static phase_t phase;
static settings_state_t state;
static slot_t slots[2];
static uint8_t loaded[SETTINGS_MAX_PAYLOAD], frame[FRAME], scratch[256];
static uint32_t scan_offset, wait_started, pending_generation;
static uint16_t pending_length;
static unsigned scan_slot, target;
static int active=-1;
static bool claimed, waiting;

static uint32_t read32(const uint8_t *p)
{
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static void write32(uint8_t *p,uint32_t value)
{
    for (unsigned i=0U;i<4U;++i) p[i]=(uint8_t)(value>>(8U*i));
}
static uint32_t base(unsigned slot)
{
    return slot==0U ? WATCH_SETTINGS_BASE_A : WATCH_SETTINGS_BASE_B;
}
static uint32_t checksum(const uint8_t *record,uint16_t length)
{
    uint8_t input[12U+SETTINGS_MAX_PAYLOAD];
    uint32_t crc=0U;
    memcpy(input,record,12U);memcpy(input+12U,record+HEADER,length);
    (void)crc32_compute(input,12U+length,&crc);
    return crc;
}
static void classify(slot_t *slot)
{
    const uint8_t *p=slot->frame;slot->kind=SLOT_UNKNOWN;
    if (slot->blank) {slot->kind=SLOT_EMPTY;return;}
    if (!slot->tail_blank || read32(p)!=MAGIC || p[4]!=1U || p[5]!=1U) return;
    uint16_t length=(uint16_t)((uint16_t)p[6]|((uint16_t)p[7]<<8));
    if (length==0U || length>SETTINGS_MAX_PAYLOAD) return;
    for (unsigned i=HEADER+length;i<FRAME;++i) if (p[i]!=0xFFU) return;
    if (checksum(p,length)!=read32(p+12U)) return;
    slot->length=length;slot->generation=read32(p+8U);
    slot->kind=read32(p+16U)==COMMIT ? SLOT_VALID : SLOT_INCOMPLETE;
}
static void release(void)
{
    if (claimed) {(void)w25q64_settings_release();claimed=false;}
}
static watch_status_t fail(watch_status_t error,bool saving)
{
    release();phase=IDLE;state.busy=false;state.ready=true;
    if (saving) state.last_save_status=error;
    else {state.last_load_status=error;state.valid=false;state.empty=false;active=-1;}
    return error;
}
watch_status_t settings_init(void)
{
    if (state.busy) return WATCH_NOT_READY;
    memset(&state,0,sizeof(state));memset(slots,0,sizeof(slots));
    for (unsigned i=0U;i<2U;++i) {slots[i].blank=true;slots[i].tail_blank=true;}
    state.busy=true;state.last_load_status=WATCH_NOT_READY;state.last_save_status=WATCH_NOT_READY;
    active=-1;claimed=false;waiting=false;scan_slot=0U;scan_offset=0U;phase=CLAIM_LOAD;
    return WATCH_OK;
}
watch_status_t settings_load(void *data,size_t length)
{
    if (data==NULL || length==0U || length>SETTINGS_MAX_PAYLOAD) return WATCH_INVALID_ARGUMENT;
    if (!state.ready) return WATCH_NOT_READY;
    if (!state.valid) return state.last_load_status;
    if (length!=state.length) return WATCH_INVALID_DATA;
    memcpy(data,loaded,length);return WATCH_OK;
}
watch_status_t settings_save(const void *data,size_t length)
{
    if (data==NULL || length==0U || length>SETTINGS_MAX_PAYLOAD) return WATCH_INVALID_ARGUMENT;
    if (!state.ready || state.busy || phase!=IDLE) return WATCH_NOT_READY;
    if (!state.valid && !state.empty) return WATCH_INVALID_DATA;
    target=active==0 ? 1U : 0U;
    if (slots[target].kind==SLOT_UNKNOWN) return WATCH_INVALID_DATA;
    watch_status_t lock=w25q64_settings_claim();
    if (lock!=WATCH_OK) return lock;
    claimed=true;
    memset(frame,0xFF,sizeof(frame));pending_length=(uint16_t)length;
    pending_generation=state.valid ? state.generation+1U : 1U;
    write32(frame,MAGIC);frame[4]=1U;frame[5]=1U;
    frame[6]=(uint8_t)length;frame[7]=(uint8_t)(length>>8);
    write32(frame+8U,pending_generation);memcpy(frame+HEADER,data,length);
    write32(frame+12U,checksum(frame,pending_length));
    state.busy=true;state.last_save_status=WATCH_NOT_READY;phase=ERASE;
    return WATCH_OK;
}
static watch_status_t select_record(void)
{
    bool a=slots[0].kind==SLOT_VALID,b=slots[1].kind==SLOT_VALID;
    if (a && b) {
        uint32_t diff=slots[0].generation-slots[1].generation;
        if (diff==0x80000000UL || (diff==0U &&
            (slots[0].length!=slots[1].length ||
             memcmp(slots[0].frame+HEADER,slots[1].frame+HEADER,slots[0].length)!=0)))
            return fail(WATCH_INVALID_DATA,false);
        active=diff==0U || diff<0x80000000UL ? 0 : 1;
    } else active=a ? 0 : b ? 1 : -1;
    state.empty=slots[0].kind==SLOT_EMPTY && slots[1].kind==SLOT_EMPTY;
    state.valid=active>=0;state.ready=true;state.busy=false;
    state.last_load_status=state.valid ? WATCH_OK : state.empty ? WATCH_NOT_READY : WATCH_INVALID_DATA;
    if (active>=0) {
        state.length=slots[active].length;state.generation=slots[active].generation;
        memcpy(loaded,slots[active].frame+HEADER,state.length);
    }
    release();phase=IDLE;return state.last_load_status;
}
watch_status_t settings_process(uint32_t now_ms)
{
    if (phase==IDLE) return state.last_save_status;
    bool saving=phase>=ERASE;
    if (phase==CLAIM_LOAD) {
        watch_status_t s=w25q64_settings_claim();
        if (s==WATCH_NOT_READY) {
            if (!waiting) {wait_started=now_ms;waiting=true;}
            if ((uint32_t)(now_ms-wait_started)>=3000U) return fail(s,saving);
            return s;
        }
        if (s!=WATCH_OK) return fail(s,saving);
        claimed=true;phase=SCAN;return WATCH_NOT_READY;
    }
    if (phase==SCAN) {
        watch_status_t s=w25q64_read(base(scan_slot)+scan_offset,scratch,sizeof(scratch));
        if (s!=WATCH_OK) return fail(s,false);
        slot_t *slot=&slots[scan_slot];
        if (scan_offset==0U) memcpy(slot->frame,scratch,FRAME);
        for (unsigned i=0U;i<sizeof(scratch);++i) {
            if (scratch[i]!=0xFFU) {
                slot->blank=false;
                if (scan_offset+i>=FRAME) slot->tail_blank=false;
            }
        }
        scan_offset+=sizeof(scratch);
        if (scan_offset==WATCH_SETTINGS_SIZE) {
            classify(slot);scan_offset=0U;
            if (++scan_slot==2U) phase=SELECT;
        }
        return WATCH_NOT_READY;
    }
    if (phase==SELECT) return select_record();
    w25q64_state_t flash;(void)w25q64_get_state(&flash);
    if (flash.busy) return WATCH_NOT_READY;
    watch_status_t s=WATCH_OK;
    switch (phase) {
    case ERASE:
        s=w25q64_settings_erase_sector(base(target));
        if (s==WATCH_OK) {slots[target].kind=SLOT_UNKNOWN;phase=WAIT_ERASE;}
        break;
    case WAIT_ERASE:
    case WAIT_BODY:
    case WAIT_COMMIT:
        if (flash.operation_status!=WATCH_OK) return fail(flash.operation_status,true);
        phase=phase==WAIT_ERASE ? WRITE_BODY : phase==WAIT_BODY ? WRITE_COMMIT : VERIFY;
        break;
    case WRITE_BODY:
        s=w25q64_settings_write(base(target),frame,HEADER+pending_length);
        if (s==WATCH_OK) phase=WAIT_BODY;
        break;
    case WRITE_COMMIT:
        write32(scratch,COMMIT);
        s=w25q64_settings_write(base(target)+16U,scratch,4U);
        if (s==WATCH_OK) phase=WAIT_COMMIT;
        break;
    case VERIFY: {
        s=w25q64_read(base(target),scratch,FRAME);
        if (s!=WATCH_OK) return fail(s,true);
        slot_t candidate;
        memset(&candidate,0,sizeof(candidate));candidate.tail_blank=true;
        memcpy(candidate.frame,scratch,FRAME);classify(&candidate);
        if (candidate.kind!=SLOT_VALID || candidate.generation!=pending_generation ||
            candidate.length!=pending_length || memcmp(candidate.frame+HEADER,frame+HEADER,pending_length)!=0)
            return fail(WATCH_IO_ERROR,true);
        slots[target]=candidate;active=(int)target;memcpy(loaded,frame+HEADER,pending_length);
        state.valid=true;state.empty=false;state.length=pending_length;state.generation=pending_generation;
        state.last_load_status=WATCH_OK;state.last_save_status=WATCH_OK;state.busy=false;
        release();phase=IDLE;return WATCH_OK;
    }
    default:return fail(WATCH_IO_ERROR,true);
    }
    return s==WATCH_OK ? WATCH_NOT_READY : fail(s,true);
}
watch_status_t settings_get_operation_status(void) {return state.last_save_status;}
watch_status_t settings_get_state(settings_state_t *result)
{
    if (result==NULL) return WATCH_INVALID_ARGUMENT;
    *result=state;return WATCH_OK;
}
