/****************************************************************************
 * boards/arm/cxd56xx/common/src/cxd56_gs2200m.c
 *
 *   Copyright 2019 Sony Home Entertainment & Sound Products Inc.
 *   Author: Masayuki Ishikawa <Masayuki.Ishikawa@jp.sony.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of Sony Semiconductor Solutions Corporation nor
 *    the names of its contributors may be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/config.h>
#include <nuttx/board.h>
#include <nuttx/spi/spi.h>
#include <nuttx/irq.h>
#include <nuttx/wireless/gs2200m.h>

#include <arch/chip/pin.h>

#include "cxd56_pinconfig.h"
#include "cxd56_spi.h"
#include "cxd56_dmac.h"
#include "cxd56_gpio.h"
#include "cxd56_gpioint.h"

#define DMA_TXCH       (CONFIG_CXD56_DMAC_SPI5_TX_CH)
#define DMA_RXCH       (CONFIG_CXD56_DMAC_SPI5_RX_CH)
#define DMA_TXCH_CFG   (CXD56_DMA_PERIPHERAL_SPI5_TX)
#define DMA_RXCH_CFG   (CXD56_DMA_PERIPHERAL_SPI5_RX)
#define SPI_TX_MAXSIZE (CONFIG_CXD56_DMAC_SPI5_TX_MAXSIZE)
#define SPI_RX_MAXSIZE (CONFIG_CXD56_DMAC_SPI5_RX_MAXSIZE)

#if defined(CONFIG_WIFI_BOARD_IS110B_HARDWARE_VERSION_10B)
/* v1.0b */
#define GS2200M_GPIO_37          (PIN_UART2_CTS)
#define GS2200M_EXT_RTC_RESET_IN (PIN_EMMC_DATA3)
#elif defined(CONFIG_WIFI_BOARD_IS110B_HARDWARE_VERSION_10C)
/* v1.0c */
#define GS2200M_GPIO_37          (PIN_EMMC_DATA2)
#define GS2200M_EXT_RTC_RESET_IN (PIN_EMMC_DATA3)
#else
/* v1.0a */
#define GS2200M_GPIO_37          (PIN_UART2_CTS)
#define GS2200M_EXT_RTC_RESET_IN (PIN_UART2_RTS)
#endif

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int  gs2200m_irq_attach(xcpt_t, FAR void *);
static void gs2200m_irq_enable(void);
static void gs2200m_irq_disable(void);
static uint32_t gs2200m_dready(int *);
static void gs2200m_reset(bool);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct gs2200m_lower_s g_wifi_lower =
{
  .attach  = gs2200m_irq_attach,
  .enable  = gs2200m_irq_enable,
  .disable = gs2200m_irq_disable,
  .dready  = gs2200m_dready,
  .reset   = gs2200m_reset
};

static FAR void *g_devhandle = NULL;
static volatile int32_t  _enable_count = 0;
static volatile uint32_t _n_called;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: gs2200m_irq_attach
 ****************************************************************************/

static int gs2200m_irq_attach(xcpt_t handler, FAR void *arg)
{
  cxd56_gpioint_config(GS2200M_GPIO_37,
                       GPIOINT_LEVEL_HIGH,
                       handler,
                       arg);
  return 0;
}

/****************************************************************************
 * Name: gs2200m_irq_enable
 ****************************************************************************/

static void gs2200m_irq_enable(void)
{
  irqstate_t flags = spin_lock_irqsave();

  wlinfo("== ec:%d called=%d \n", _enable_count, _n_called++);

  if (1 == _enable_count)
    {
      /* NOTE: This would happen if we received an event */

      return;
    }

  _enable_count++;

  if (1 == _enable_count)
    {
      cxd56_gpioint_enable(GS2200M_GPIO_37);
    }

  spin_unlock_irqrestore(flags);
}

/****************************************************************************
 * Name: gs2200m_irq_disable
 ****************************************************************************/

static void gs2200m_irq_disable(void)
{
  irqstate_t flags = spin_lock_irqsave();

  wlinfo("== ec:%d called=%d \n", _enable_count, _n_called++);

  _enable_count--;

  if (0 == _enable_count)
    {
      cxd56_gpioint_disable(GS2200M_GPIO_37);
    }

  spin_unlock_irqrestore(flags);
}

/****************************************************************************
 * Name: gs2200m_dready
 ****************************************************************************/

static uint32_t gs2200m_dready(int *ec)
{
  irqstate_t flags = spin_lock_irqsave();

  uint32_t r = cxd56_gpio_read(GS2200M_GPIO_37);

  if (ec)
    {
      /* Copy enable count (just for debug) */

      *ec = _enable_count;
    }

  spin_unlock_irqrestore(flags);
  return r;
}

/****************************************************************************
 * Name: gs2200m_reset
 ****************************************************************************/

static void gs2200m_reset(bool reset)
{
  cxd56_gpio_write(GS2200M_EXT_RTC_RESET_IN, !reset);
}

/****************************************************************************
 * Name: spi_pincontrol
 *
 * Description:
 *   Configure the SPI pin
 *
 * Input Parameter:
 *   on - true: enable pin, false: disable pin
 ****************************************************************************/

static void spi_pincontrol(int bus, bool on)
{
  if (bus == 5)
    {
#ifdef CONFIG_CXD56_SPI5_PINMAP_EMMC
      if (on)
        {
          CXD56_PIN_CONFIGS(PINCONFS_EMMCA_SPI5);
        }
      else
        {
          CXD56_PIN_CONFIGS(PINCONFS_EMMCA_GPIO);
        }
#endif
#ifdef CONFIG_CXD56_SPI5_PINMAP_SDIO
      if (on)
        {
          CXD56_PIN_CONFIGS(PINCONFS_SDIOA_SPI5);
        }
      else
        {
          CXD56_PIN_CONFIGS(PINCONFS_SDIOA_GPIO);
        }
#endif
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: board_gs2200m_initialize
 ****************************************************************************/

int board_gs2200m_initialize(FAR const char *devpath, int bus)
{
  FAR struct spi_dev_s *spi;
  DMA_HANDLE    hdl;
  dma_config_t  conf;

  wlinfo("Initializing GS2200M..\n");

  if (!g_devhandle)
    {
      /* Change UART2 to GPIO */

      CXD56_PIN_CONFIGS(PINCONFS_UART2_GPIO);
      cxd56_gpio_config(GS2200M_GPIO_37, true);
      cxd56_gpio_config(GS2200M_EXT_RTC_RESET_IN, false);

      /* Initialize spi device */

      spi = cxd56_spibus_initialize(bus);

      if (!spi)
        {
          wlerr("ERROR: Failed to initialize spi%d.\n", bus);
          return -ENODEV;
        }

      hdl = cxd56_dmachannel(DMA_TXCH, SPI_TX_MAXSIZE);
      if (hdl)
        {
          conf.channel_cfg = DMA_TXCH_CFG;
          conf.dest_width  = CXD56_DMAC_WIDTH8;
          conf.src_width   = CXD56_DMAC_WIDTH8;
          cxd56_spi_dmaconfig(bus, CXD56_SPI_DMAC_CHTYPE_TX, hdl, &conf);
        }

      hdl = cxd56_dmachannel(DMA_RXCH, SPI_RX_MAXSIZE);
      if (hdl)
        {
          conf.channel_cfg = DMA_RXCH_CFG;
          conf.dest_width  = CXD56_DMAC_WIDTH8;
          conf.src_width   = CXD56_DMAC_WIDTH8;
          cxd56_spi_dmaconfig(bus, CXD56_SPI_DMAC_CHTYPE_RX, hdl, &conf);
        }

      /* Enable SPI5 */

      spi_pincontrol(bus, true);

      g_devhandle = gs2200m_register(devpath, spi, &g_wifi_lower);

      if (!g_devhandle)
        {
          wlerr("ERROR: Failed to register gs2200m driver.\n");
          return -ENODEV;
        }
    }

  return OK;
}
