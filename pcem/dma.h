void dma_init();
void dma16_init();
void ps2_dma_init();
void dma_reset();

#define DMA_NODATA -1
#define DMA_OVER 0x10000

void readdma0();
int readdma1();
uint8_t readdma2();
int readdma3();

void writedma2(uint8_t temp);

int dma_channel_read(int channel);
int dma_channel_write(int channel, uint16_t val);

void dma_bm_read(uint32_t PhysAddress, uint8_t *DataRead, uint32_t TotalSize, int TransferSize);
void dma_bm_write(uint32_t PhysAddress, const uint8_t *DataWrite, uint32_t TotalSize, int TransferSize);
