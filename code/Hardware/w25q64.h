#ifndef WATCH_W25Q64_H
#define WATCH_W25Q64_H
#include "watch_types.h"
#define W25Q64_TEST_BASE 0x7FF000UL
#define W25Q64_TEST_SIZE 4096U
/* 保留历史模块名。PY型号是ID与专用手册映射，非丝印实测。 */
typedef enum { W25Q64_MODEL_UNCONFIRMED, W25Q64_MODEL_PY25Q64HA_ID } w25q64_model_t;
typedef enum { FLASH_TEST_IDLE, FLASH_TEST_SCAN, FLASH_TEST_FULL, FLASH_TEST_CROSS,
    FLASH_TEST_LAST, FLASH_TEST_ERASE, FLASH_TEST_PASS, FLASH_TEST_FAILED,
    FLASH_TEST_NONEMPTY, FLASH_TEST_CLEANING, FLASH_TEST_CLEAN } flash_test_stage_t;
typedef struct {
    uint32_t raw_jedec_id;
    uint32_t stable_samples;
    uint32_t sample_ms;
    uint32_t capacity_bytes; /* 仅ID映射的标称容量，未知ID为0；非容量实测。 */
    w25q64_model_t model;
    watch_status_t status;
    bool raw_valid;
    bool capacity_confirmed;
    bool write_enabled;
    bool busy, owned, dirty;
    watch_status_t operation_status, test_status;
    flash_test_stage_t test_stage;
    flash_test_stage_t failed_stage;
    uint32_t test_progress;
    uint8_t sr0, sr1, config;
} w25q64_state_t;
/* OK仅表示原始回应非全零/全FF；失败输出0或本次无效回应。 */
watch_status_t w25q64_identify(uint32_t *jedec_id);
/* 每秒调用一次：一次事务，不等待器件擦除或编程。 */
watch_status_t w25q64_probe(uint32_t now_ms);
watch_status_t w25q64_get_state(w25q64_state_t *result);
/* 5ms任务调用；一次只推进一个阶段，不忙等器件完成。 */
watch_status_t w25q64_process(uint32_t now_ms);
/* 单次读取1..256字节，错误时不能使用缓冲内容。 */
watch_status_t w25q64_read(uint32_t offset, uint8_t *data, size_t length);
/* 仅在本次开机整区查空后认领的测试区内开放。OK表示排队成功，
 * 非已落盘；最终结果为operation_status。写数据立即复制，最多256字节，支持跨页。 */
watch_status_t w25q64_write(uint32_t offset, const uint8_t *data, size_t length);
watch_status_t w25q64_erase_sector(uint32_t offset);
/* F07-P02参数持久化：受限开放两个设置扇区。OK表示排队成功，非已落盘；
 * 最终结果为operation_status。数据立即复制，支持跨页，最多256字节。
 * claim覆盖完整事务，release仅在无底层操作时成功；P01与参数事务互斥。 */
watch_status_t w25q64_settings_claim(void);
watch_status_t w25q64_settings_release(void);
watch_status_t w25q64_settings_write(uint32_t offset, const uint8_t *data, size_t length);
watch_status_t w25q64_settings_erase_sector(uint32_t offset);
/* F07-P03历史存储：开放WATCH_HISTORY_BASE至WATCH_HISTORY_END受限读写。
 * OK表示排队成功，非已落盘；最终结果为operation_status。 */
watch_status_t w25q64_history_claim(void);
watch_status_t w25q64_history_release(void);
watch_status_t w25q64_history_write(uint32_t offset, const uint8_t *data, size_t length);
watch_status_t w25q64_history_erase_sector(uint32_t offset);
/* 显式启动，不自动擦除非空区；恢复只接受本次开机owned+dirty。
 * 断电后无法证明归属的非空区只报错，不能按测试残留清理。 */
watch_status_t w25q64_test_start(void);
watch_status_t w25q64_test_recover(void);
#endif
