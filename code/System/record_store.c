#include "record_store.h"
#include "board_config.h"
#include "w25q64.h"
#include "crc32.h"
#include <string.h>

/* 32字节扇区头+127条32字节记录；所有字段显式小端编码。
 * 每条记录及扇区头分别CRC校验，最后单独写入提交标记。
 * ID递增且不回绕；保留一个备用扇区，新记录提交后才淘汰最旧扇区。
 * 启动只读；未知格式保留。合法扇区的未提交尾部隔离后改用备用扇区。 */
enum { SECTOR_BYTES=4096U, FRAME_BYTES=32U, RECORDS=127U,
       SECTOR_MAGIC=0x314C4E4AU, RECORD_MAGIC=0x31524348U,
       SECTOR_COMMIT=0xA55AC33CU, RECORD_COMMIT=0x5AC33CA5U };
#ifndef RECORD_STORE_SECTOR_COUNT
#define RECORD_STORE_SECTOR_COUNT ((WATCH_HISTORY_END-WATCH_HISTORY_BASE)/SECTOR_BYTES)
#endif
_Static_assert(RECORD_STORE_SECTOR_COUNT>=3U && RECORD_STORE_SECTOR_COUNT<=1791U,
               "History needs a spare sector and must fit the allocated partition");
_Static_assert(WATCH_HISTORY_BASE+RECORD_STORE_SECTOR_COUNT*SECTOR_BYTES<=WATCH_HISTORY_END,
               "History must exclude the Flash test sector");
typedef enum { UNKNOWN, CANDIDATE, FORMATTED } sector_kind_t;
typedef struct {
    uint32_t first_id, valid[4];
    uint16_t count, used;
    uint8_t kind;
    bool retained, sealed;
} sector_t;
typedef enum { IDLE, LOAD_CLAIM, LOAD_HEADER, LOAD_RECORDS, INDEX,
    CHECK_EMPTY, ERASE, WAIT_ERASE, HEADER_BODY, WAIT_HEADER_BODY,
    HEADER_MARK, WAIT_HEADER_MARK, HEADER_VERIFY,
    RECORD_BODY, WAIT_RECORD_BODY, RECORD_MARK, WAIT_RECORD_MARK, RECORD_VERIFY } work_t;
static sector_t sectors[RECORD_STORE_SECTOR_COUNT];
static uint16_t order[RECORD_STORE_SECTOR_COUNT];
static unsigned order_count, scan_sector, scan_record, index_sector, target, target_slot;
static uint32_t scan_offset, pending_id;
static work_t work;
static record_store_state_t state;
static uint8_t io[256], header[FRAME_BYTES], encoded[FRAME_BYTES];
static bool claimed, new_sector, index_ambiguous;

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static void put32(uint8_t *p,uint32_t n)
{
    for (unsigned i=0U;i<4U;++i) p[i]=(uint8_t)(n>>(8U*i));
}
static uint32_t crc(const uint8_t *p)
{
    uint32_t result=0U;(void)crc32_compute(p,24U,&result);return result;
}
static bool blank(const uint8_t *p,size_t n)
{
    for (size_t i=0U;i<n;++i) if (p[i]!=0xFFU) return false;
    return true;
}
static uint32_t sector_base(unsigned n)
{
    return WATCH_HISTORY_BASE+(uint32_t)n*SECTOR_BYTES;
}
static bool has(const sector_t *s,unsigned slot)
{
    return (s->valid[slot/32U]&(1UL<<(slot%32U)))!=0U;
}
static uint32_t first_valid(const sector_t *s)
{
    for (unsigned i=0U;i<RECORDS;++i) if (has(s,i)) return s->first_id+i;
    return 0U;
}
static uint32_t last_valid(const sector_t *s)
{
    for (unsigned i=RECORDS;i>0U;--i) if (has(s,i-1U)) return s->first_id+i-1U;
    return 0U;
}
static bool time_valid(uint32_t utc)
{
    return utc>=946684800UL && utc<=4102444799UL;
}
static bool decode_header(const uint8_t *p,uint32_t *first)
{
    if (get32(p)!=SECTOR_MAGIC || p[4]!=1U || p[5]!=FRAME_BYTES ||
        p[6]!=FRAME_BYTES || p[7]!=0xFFU || get32(p+12U)!=RECORDS ||
        !blank(p+16U,8U) || get32(p+24U)!=crc(p) || get32(p+28U)!=SECTOR_COMMIT)
        return false;
    *first=get32(p+8U);
    return *first>0U && *first<UINT32_MAX;
}
static bool decode_record(const uint8_t *p,uint32_t id,record_t *out)
{
    uint32_t utc=get32(p+8U);
    if (id==0U || id==UINT32_MAX || get32(p)!=RECORD_MAGIC || get32(p+4U)!=id || p[19]!=1U ||
        p[18]!=(utc==0U ? 0U : 1U) || (utc!=0U && !time_valid(utc)) ||
        !blank(p+20U,4U) || get32(p+24U)!=crc(p) || get32(p+28U)!=RECORD_COMMIT)
        return false;
    if (out!=NULL) {
        out->utc_seconds=utc;out->value=get32(p+12U);
        out->type=(uint16_t)((uint16_t)p[16]|((uint16_t)p[17]<<8));
    }
    return true;
}
static void encode_record(const record_t *record)
{
    memset(encoded,0xFF,sizeof(encoded));
    put32(encoded,RECORD_MAGIC);put32(encoded+4U,pending_id);
    put32(encoded+8U,record->utc_seconds);put32(encoded+12U,record->value);
    encoded[16]=(uint8_t)record->type;encoded[17]=(uint8_t)(record->type>>8);
    encoded[18]=record->utc_seconds==0U ? 0U : 1U;encoded[19]=1U;
    put32(encoded+24U,crc(encoded));
}
static void unlock(void)
{
    if (claimed) {(void)w25q64_history_release();claimed=false;}
}
static void readonly(watch_status_t status)
{
    state.readonly=true;state.load_status=status;
}
static void finish_phase(void)
{
    state.busy=false;state.ready=true;
    state.phase=state.readonly ? RECORD_STORE_READONLY_ERROR : RECORD_STORE_READY;
    work=IDLE;unlock();
}
static watch_status_t append_failed(watch_status_t status)
{
    state.last_append_status=status;readonly(status);
    finish_phase();return status;
}
static void update_window(void)
{
    state.count=0U;state.oldest_id=0U;state.newest_id=0U;
    for (unsigned i=0U;i<order_count;++i) {
        sector_t *s=&sectors[order[i]];
        state.count+=s->count;
        uint32_t first=first_valid(s),last=last_valid(s);
        if (state.oldest_id==0U || first<state.oldest_id) state.oldest_id=first;
        if (last>state.newest_id) state.newest_id=last;
    }
}
watch_status_t record_store_init(void)
{
    if (state.busy) return WATCH_NOT_READY;
    memset(sectors,0,sizeof(sectors));memset(&state,0,sizeof(state));
    state.capacity=(RECORD_STORE_SECTOR_COUNT-1U)*RECORDS;
    state.busy=true;state.phase=RECORD_STORE_LOADING;
    state.last_append_status=WATCH_NOT_READY;state.last_read_status=WATCH_NOT_READY;
    state.load_status=WATCH_NOT_READY;
    order_count=0U;scan_sector=0U;scan_record=0U;scan_offset=0U;index_sector=0U;
    claimed=false;index_ambiguous=false;work=LOAD_CLAIM;return WATCH_OK;
}
static watch_status_t load_step(void)
{
    if (work==LOAD_CLAIM) {
        watch_status_t status=w25q64_history_claim();
        if (status==WATCH_OK) {claimed=true;work=LOAD_HEADER;}
        return status==WATCH_OK ? WATCH_NOT_READY : status;
    }
    if (work==LOAD_HEADER) {
        if (scan_sector==RECORD_STORE_SECTOR_COUNT) {
            state.scan_progress=100U;work=INDEX;return WATCH_NOT_READY;
        }
        watch_status_t status=w25q64_read(sector_base(scan_sector),io,FRAME_BYTES);
        if (status!=WATCH_OK) {
            readonly(status);++scan_sector;return status;
        }
        sector_t *s=&sectors[scan_sector];uint32_t first=0U;
        if (blank(io,FRAME_BYTES)) s->kind=CANDIDATE;
        else if (decode_header(io,&first)) {
            s->kind=FORMATTED;s->first_id=first;
            scan_record=0U;work=LOAD_RECORDS;return WATCH_NOT_READY;
        } else readonly(WATCH_INVALID_DATA);
        ++scan_sector;state.scan_progress=(uint32_t)(scan_sector*100U/RECORD_STORE_SECTOR_COUNT);
        return WATCH_NOT_READY;
    }
    if (work==LOAD_RECORDS) {
        unsigned n=RECORDS-scan_record;if (n>8U) n=8U;
        watch_status_t status=w25q64_read(sector_base(scan_sector)+FRAME_BYTES*(scan_record+1U),
                                         io,n*FRAME_BYTES);
        if (status!=WATCH_OK) {
            readonly(status);sectors[scan_sector].sealed=true;
            ++scan_sector;work=LOAD_HEADER;return status;
        }
        sector_t *s=&sectors[scan_sector];
        for (unsigned i=0U;i<n;++i) {
            const uint8_t *p=io+i*FRAME_BYTES;unsigned slot=scan_record+i;
            if (blank(p,FRAME_BYTES)) {s->sealed=true;continue;}
            s->used=(uint16_t)(slot+1U);
            if (get32(p+28U)!=RECORD_COMMIT) {
                s->sealed=true;continue; /* 本格式所有权内的未提交尾部不计入记录。 */
            }
            if (s->first_id>UINT32_MAX-slot) {
                readonly(WATCH_INVALID_DATA);s->sealed=true;continue;
            }
            if (!decode_record(p,s->first_id+slot,NULL)) {
                readonly(WATCH_INVALID_DATA);s->sealed=true;continue;
            }
            if (s->sealed) readonly(WATCH_INVALID_DATA);
            s->valid[slot/32U]|=1UL<<(slot%32U);++s->count;
        }
        scan_record+=n;
        if (scan_record==RECORDS) {
            /* 连续合法前缀之后只有全FF时仍允许追加；真正残留或缺口隔离。 */
            s->sealed=s->used!=s->count;
            ++scan_sector;work=LOAD_HEADER;
            state.scan_progress=(uint32_t)(scan_sector*100U/RECORD_STORE_SECTOR_COUNT);
        }
        return WATCH_NOT_READY;
    }
    /* 分批建立有序扇区索引，避免在单个调度中对全区排序。 */
    for (unsigned batch=0U;batch<32U && index_sector<RECORD_STORE_SECTOR_COUNT;++batch) {
        unsigned idx=index_sector++;
        if (sectors[idx].count==0U) continue;
        unsigned pos=order_count;
        uint32_t first=sectors[idx].first_id;
        while (pos>0U && sectors[order[pos-1U]].first_id>first) {
            order[pos]=order[pos-1U];--pos;
        }
        order[pos]=(uint16_t)idx;++order_count;
        break;
    }
    if (index_sector!=RECORD_STORE_SECTOR_COUNT) return WATCH_NOT_READY;
    for (unsigned i=1U;i<order_count;++i) {
        sector_t *previous=&sectors[order[i-1U]],*next=&sectors[order[i]];
        if (previous->first_id==next->first_id || last_valid(previous)>=first_valid(next)) {
            index_ambiguous=true;readonly(WATCH_INVALID_DATA);
        }
    }
    /* 顺序不明确时保留全部物理副本，只读且拒绝歧义ID，不擅自淘汰一份。 */
    unsigned drop=!index_ambiguous && order_count>RECORD_STORE_SECTOR_COUNT-1U ? 1U : 0U;
    if (drop!=0U) {
        memmove(order,order+1U,(order_count-1U)*sizeof(order[0]));--order_count;
    }
    for (unsigned i=0U;i<order_count;++i) sectors[order[i]].retained=true;
    update_window();
    if (!state.readonly) state.load_status=WATCH_OK;
    finish_phase();return state.load_status;
}
watch_status_t record_store_append(const record_t *record)
{
    if (record==NULL || (record->utc_seconds!=0U && !time_valid(record->utc_seconds)))
        return WATCH_INVALID_ARGUMENT;
    if (!state.ready || state.busy) return WATCH_NOT_READY;
    if (state.readonly) return WATCH_INVALID_DATA;
    if (state.newest_id>=UINT32_MAX-1U) return WATCH_INVALID_DATA;
    pending_id=state.count==0U ? 1U : state.newest_id+1U;
    new_sector=true;
    if (order_count!=0U) {
        target=order[order_count-1U];sector_t *s=&sectors[target];
        if (!s->sealed && s->used<RECORDS && s->first_id+s->used==pending_id) {
            target_slot=s->used;new_sector=false;
        }
    }
    if (new_sector) {
        unsigned start=order_count==0U ? 0U : (target+1U)%RECORD_STORE_SECTOR_COUNT;
        bool found=false;
        for (unsigned i=0U;i<RECORD_STORE_SECTOR_COUNT;++i) {
            unsigned candidate=(start+i)%RECORD_STORE_SECTOR_COUNT;
            if (!sectors[candidate].retained && sectors[candidate].kind!=UNKNOWN) {
                target=candidate;found=true;break;
            }
        }
        if (!found) return WATCH_INVALID_DATA;
        target_slot=0U;
    }
    watch_status_t status=w25q64_history_claim();
    if (status!=WATCH_OK) return status;
    claimed=true;encode_record(record);state.busy=true;
    state.phase=RECORD_STORE_APPENDING;state.last_append_status=WATCH_NOT_READY;
    scan_offset=0U;
    work=new_sector ? (sectors[target].kind==CANDIDATE ? CHECK_EMPTY : ERASE) : RECORD_BODY;
    return WATCH_OK;
}
watch_status_t record_store_process(uint32_t now_ms)
{
    (void)now_ms;
    if (work==IDLE) return state.last_append_status;
    if (work>=LOAD_CLAIM && work<=INDEX) return load_step();
    w25q64_state_t flash;(void)w25q64_get_state(&flash);
    if (flash.busy) return WATCH_NOT_READY;
    watch_status_t status=WATCH_OK;
    switch (work) {
    case CHECK_EMPTY:
        status=w25q64_read(sector_base(target)+scan_offset,io,sizeof(io));
        if (status!=WATCH_OK) return append_failed(status);
        if (!blank(io,sizeof(io))) return append_failed(WATCH_INVALID_DATA);
        scan_offset+=sizeof(io);
        if (scan_offset==SECTOR_BYTES) work=HEADER_BODY;
        break;
    case ERASE:
        status=w25q64_history_erase_sector(sector_base(target));
        if (status==WATCH_OK) work=WAIT_ERASE;
        break;
    case WAIT_ERASE:
    case WAIT_HEADER_BODY:
    case WAIT_HEADER_MARK:
    case WAIT_RECORD_BODY:
    case WAIT_RECORD_MARK:
        if (flash.operation_status!=WATCH_OK) return append_failed(flash.operation_status);
        work=work==WAIT_ERASE ? HEADER_BODY : work==WAIT_HEADER_BODY ? HEADER_MARK :
             work==WAIT_HEADER_MARK ? HEADER_VERIFY : work==WAIT_RECORD_BODY ? RECORD_MARK : RECORD_VERIFY;
        break;
    case HEADER_BODY:
        memset(header,0xFF,sizeof(header));put32(header,SECTOR_MAGIC);
        header[4]=1U;header[5]=FRAME_BYTES;header[6]=FRAME_BYTES;
        put32(header+8U,pending_id);put32(header+12U,RECORDS);put32(header+24U,crc(header));
        status=w25q64_history_write(sector_base(target),header,28U);
        if (status==WATCH_OK) work=WAIT_HEADER_BODY;
        break;
    case HEADER_MARK:
        put32(io,SECTOR_COMMIT);
        status=w25q64_history_write(sector_base(target)+28U,io,4U);
        if (status==WATCH_OK) work=WAIT_HEADER_MARK;
        break;
    case HEADER_VERIFY: {
        status=w25q64_read(sector_base(target),io,FRAME_BYTES);uint32_t first=0U;
        if (status!=WATCH_OK) return append_failed(status);
        if (!decode_header(io,&first) || first!=pending_id) return append_failed(WATCH_IO_ERROR);
        work=RECORD_BODY;break;
    }
    case RECORD_BODY:
        status=w25q64_history_write(sector_base(target)+FRAME_BYTES*(target_slot+1U),encoded,28U);
        if (status==WATCH_OK) work=WAIT_RECORD_BODY;
        break;
    case RECORD_MARK:
        put32(io,RECORD_COMMIT);
        status=w25q64_history_write(sector_base(target)+FRAME_BYTES*(target_slot+1U)+28U,io,4U);
        if (status==WATCH_OK) work=WAIT_RECORD_MARK;
        break;
    case RECORD_VERIFY:
        status=w25q64_read(sector_base(target)+FRAME_BYTES*(target_slot+1U),io,FRAME_BYTES);
        if (status!=WATCH_OK) return append_failed(status);
        if (!decode_record(io,pending_id,NULL) || memcmp(io,encoded,28U)!=0)
            return append_failed(WATCH_IO_ERROR);
        if (new_sector) {
            sector_t *s=&sectors[target];memset(s,0,sizeof(*s));
            s->kind=FORMATTED;s->first_id=pending_id;s->retained=true;
            order[order_count++]=(uint16_t)target;
            if (order_count>RECORD_STORE_SECTOR_COUNT-1U) {
                sectors[order[0]].retained=false;
                memmove(order,order+1U,(order_count-1U)*sizeof(order[0]));--order_count;
            }
        }
        sectors[target].valid[target_slot/32U]|=1UL<<(target_slot%32U);
        ++sectors[target].count;sectors[target].used=(uint16_t)(target_slot+1U);
        update_window();state.last_append_status=WATCH_OK;finish_phase();return WATCH_OK;
    default:return append_failed(WATCH_IO_ERROR);
    }
    return status==WATCH_OK ? WATCH_NOT_READY : append_failed(status);
}
static watch_status_t read_error(watch_status_t status)
{
    state.last_read_status=status;return status;
}
watch_status_t record_store_read(uint32_t id,record_t *record)
{
    if (record==NULL) return read_error(WATCH_INVALID_ARGUMENT);
    memset(record,0,sizeof(*record));
    if (!state.ready || state.busy) return read_error(WATCH_NOT_READY);
    if (id==0U || state.count==0U || id>state.newest_id) return read_error(WATCH_INVALID_ARGUMENT);
    if (id<state.oldest_id) return read_error(WATCH_INVALID_DATA);
    int found=-1;unsigned slot=0U;
    for (unsigned i=0U;i<order_count;++i) {
        sector_t *s=&sectors[order[i]];
        if (id>=s->first_id && id-s->first_id<RECORDS && has(s,(unsigned)(id-s->first_id))) {
            if (found>=0) return read_error(WATCH_INVALID_DATA);
            found=(int)order[i];slot=(unsigned)(id-s->first_id);
        }
    }
    if (found<0) return read_error(WATCH_INVALID_DATA);
    watch_status_t status=w25q64_history_claim();
    if (status!=WATCH_OK) {state.last_read_status=status;return status;}
    claimed=true;
    status=w25q64_read(sector_base((unsigned)found)+FRAME_BYTES*(slot+1U),io,FRAME_BYTES);
    if (status==WATCH_OK && !decode_record(io,id,record)) {
        status=WATCH_INVALID_DATA;
        sector_t *s=&sectors[(unsigned)found];
        s->valid[slot/32U]&=~(1UL<<(slot%32U));--s->count;
        if (s->count==0U) {
            for (unsigned i=0U;i<order_count;++i) if (order[i]==(unsigned)found) {
                memmove(order+i,order+i+1U,(order_count-i-1U)*sizeof(order[0]));
                --order_count;s->retained=false;break;
            }
        }
        update_window();
    }
    unlock();state.last_read_status=status;
    if (status!=WATCH_OK) {
        memset(record,0,sizeof(*record));readonly(status);state.phase=RECORD_STORE_READONLY_ERROR;
    }
    return status;
}
watch_status_t record_store_get_state(record_store_state_t *result)
{
    if (result==NULL) return WATCH_INVALID_ARGUMENT;
    *result=state;return WATCH_OK;
}
