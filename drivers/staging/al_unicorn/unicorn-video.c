/*
    unicorn-video.c - V4L2 driver for FPGA UNICORN

    Copyright (c) 2010 Aldebaran robotics
    joseph pinkasfeld joseph.pinkasfeld@gmail.com
    Ludovic SMAL <lsmal@aldebaran-robotics.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#define __UNICORN_VIDEO_C

#include "unicorn.h"

#ifdef CONFIG_AL_UNICORN_WIDTH_VIDEO_SUPPORT
#include "unicorn-video.h"



static unsigned int video_nr[] = {[0 ... (UNICORN_MAXBOARDS - 1)] = UNSET };
module_param_array(video_nr, int, NULL, 0444);
MODULE_PARM_DESC(video_nr, "video device numbers");

module_param(video_debug, int, 0644);
MODULE_PARM_DESC(video_debug, "enable debug messages [video]");

unsigned int vid_limit = 16;
module_param(vid_limit, int, 0644);
MODULE_PARM_DESC(vid_limit, "capture memory limit in megabytes");



int unicorn_start_video_dma(struct unicorn_dev *dev,
          struct unicorn_buffer *buf,
          struct unicorn_fh *fh)
{
  unsigned long addr=0;
  int size = 0;

  dprintk_video(1, dev->name, "%s() channel:%d input:%d fps:%d\n", __func__, fh->channel, fh->input, dev->fps_limit[fh->channel]);

  dev->global_register->video[fh->channel].ctrl |= DMA_CONTROL_RESET;

  dev->interrupts_controller->irq.ctrl |=

   /* Init IT Channel 0 */
   IT_DMA_CHAN_0_TX_BUFF_0_END   |
   IT_DMA_CHAN_0_TX_BUFF_1_END   |
   IT_DMA_CHAN_0_ERROR           |
   IT_DMA_CHAN_0_FIFO_FULL_ERROR |
   IT_VIDEO_CHANNEL_0_OF_TRAME   |

   /* Init IT Channel 1 */
   IT_DMA_CHAN_1_TX_BUFF_0_END   |
   IT_DMA_CHAN_1_TX_BUFF_1_END   |
   IT_DMA_CHAN_1_ERROR           |
   IT_DMA_CHAN_1_FIFO_FULL_ERROR |
   IT_VIDEO_CHANNEL_1_OF_TRAME	 |

   /* AHB32 Error */
   IT_ABH32_ERROR                |
   IT_ABH32_FIFO_RX_ERROR        |
   IT_ABH32_FIFO_TX_ERROR        |
   IT_RT;

  dev->global_register->video_in_reset |= 0x01 << fh->input;

  dev->global_register->video[fh->channel].ctrl &= ~VIDEO_CONTROL_INPUT_SEL_MASK;
  dev->global_register->video[fh->channel].ctrl |= (fh->input)<<VIDEO_CONTROL_INPUT_SEL_POS;

//  dev->global_register->video[fh->channel].ctrl |= VIDEO_CONTROL_TIMESTAMP_INSERT;

  dev->global_register->video[fh->channel].nb_lines = fh->height;

  dev->global_register->video[fh->channel].nb_pixels = fh->width;

  if(dev->fps_limit[fh->channel]!=0)
  {
    dev->global_register->video[fh->channel].nb_us_inhibit = (1000/dev->fps_limit[fh->channel])*1000;
  }
  else
  {
    dev->global_register->video[fh->channel].nb_us_inhibit = 0;
  }

  dev->global_register->video_in_reset &= ~ (0x01 << fh->input) ;

  size = fh->width*fh->height*(fh->fmt->depth>>3)/UNICORN_DMA_BLOC_SIZE;
  dev->pcie_dma->dma[fh->channel].buff[0].size = size;
  dev->pcie_dma->dma[fh->channel].buff[1].size = 0;
  addr = videobuf_to_dma_contig(&buf->vb);
  dev->pcie_dma->dma[fh->channel].buff[0].addr = addr;
  dev->pcie_dma->dma[fh->channel].buff[1].addr = 0;

  dev->pcie_dma->dma[fh->channel].ctrl &= ~DMA_CONTROL_AUTO_START;
  dev->pcie_dma->dma[fh->channel].ctrl |= DMA_CONTROL_START;
  dev->global_register->video[fh->channel].ctrl |= VIDEO_CONTROL_ENABLE;

  dprintk_video(1, dev->name, "dma_0_start_add=%x dma_1_start_add=%x", (unsigned int)&dev->pcie_dma->dma[0], (unsigned int)&dev->pcie_dma->dma[1]);

  return 0;
}

int unicorn_recover_fifo_full_error(struct unicorn_dev *dev,
                                    int channel)
{

  dprintk_video(1,dev->name,"%s() channel:%d",__func__, channel);

  spin_lock(&dev->slock);

    dev->fifo_full_error |= 1<<channel;
    dev->global_register->video[channel].ctrl &= ~VIDEO_CONTROL_ENABLE;
    dev->global_register->video_in_reset  |=  (0x01 << (
        (dev->global_register->video[channel].ctrl & VIDEO_CONTROL_INPUT_SEL_MASK)>>VIDEO_CONTROL_INPUT_SEL_POS));
    dev->pcie_dma->dma[channel].ctrl |= DMA_CONTROL_RESET;

  spin_unlock(&dev->slock);


  return 0;

}

int unicorn_continue_video_dma(struct unicorn_dev *dev,
                               struct unicorn_buffer *buf,
                               struct unicorn_fh *fh,
                               int buff_index)
{
  unsigned long addr=0;
  int size=0;
  dprintk_video(1, dev->name, "%s() channel %d buf %d \n", __func__, fh->channel,buff_index);
  size = fh->width*fh->height*(fh->fmt->depth>>3)/UNICORN_DMA_BLOC_SIZE;
  dev->pcie_dma->dma[fh->channel].buff[buff_index].size = size;
  addr = videobuf_to_dma_contig(&buf->vb);
  dev->pcie_dma->dma[fh->channel].buff[buff_index].addr = addr;

  return 0;
}

int unicorn_video_dma_flipflop_buf(struct unicorn_dev *dev,
          struct unicorn_buffer *buf,
          struct unicorn_fh *fh)
{
  unsigned long addr=0;
  int size=0;

  dprintk_video(1, dev->name, "%s() channel %d buff[0].size=%d buff[1].size=%d\n", __func__, fh->channel,
		  dev->pcie_dma->dma[fh->channel].buff[0].size,
		  dev->pcie_dma->dma[fh->channel].buff[1].size);

  if(dev->pcie_dma->dma[fh->channel].buff[0].size == 0)
  {
    size = fh->width*fh->height*(fh->fmt->depth>>3)/UNICORN_DMA_BLOC_SIZE;
    dev->pcie_dma->dma[fh->channel].ctrl |= DMA_CONTROL_AUTO_START;
    dev->pcie_dma->dma[fh->channel].buff[0].size = size;
    addr = videobuf_to_dma_contig(&buf->vb);
    dev->pcie_dma->dma[fh->channel].buff[0].addr = addr;
    return 1;

  }

  if(dev->pcie_dma->dma[fh->channel].buff[1].size == 0)
  {
    size = fh->width*fh->height*(fh->fmt->depth>>3)/UNICORN_DMA_BLOC_SIZE;
    dev->pcie_dma->dma[fh->channel].ctrl |= DMA_CONTROL_AUTO_START;
    dev->pcie_dma->dma[fh->channel].buff[1].size = size;
    addr = videobuf_to_dma_contig(&buf->vb);
    dev->pcie_dma->dma[fh->channel].buff[1].addr = addr;
    return 1;

  }

  return 0;
}

int unicorn_video_change_fps(struct unicorn_dev *dev, struct unicorn_fh *fh)
{
  int channel = fh->channel;

  spin_lock(&dev->slock);

  dev->fifo_full_error |= 1<<channel;
  dev->global_register->video[channel].ctrl &= ~VIDEO_CONTROL_ENABLE;
  dev->global_register->video_in_reset  |=  (0x01 << (
      (dev->global_register->video[channel].ctrl & VIDEO_CONTROL_INPUT_SEL_MASK)>>VIDEO_CONTROL_INPUT_SEL_POS));
  dev->pcie_dma->dma[channel].ctrl |= DMA_CONTROL_RESET;

  spin_unlock(&dev->slock);

  return 0;
}


struct video_device *unicorn_vdev_init(struct unicorn_dev *dev,
    struct pci_dev *pci,
    struct video_device *template,
    char *type)
{
  struct video_device *vfd;
  dprintk_video(1, dev->name, "%s() %d\n", __func__,template->index);

  vfd = video_device_alloc();
  if (NULL == vfd)
  {
    return NULL;
  }
  *vfd = *template;
  vfd->v4l2_dev = &dev->v4l2_dev;
  vfd->release = video_device_release;
  snprintf(vfd->name, sizeof(vfd->name), "%s %s ", dev->name, type);
  video_set_drvdata(vfd, dev);
  return vfd;
}

void unicorn_video_unregister(struct unicorn_dev *dev, int chan_num)
{
  if (dev->video_dev[chan_num]) {
    if (video_is_registered(dev->video_dev[chan_num])) {
     video_unregister_device(dev->video_dev[chan_num]);
    }
    else {
      video_device_release(dev->video_dev[chan_num]);
    }

    dev->video_dev[chan_num] = NULL;

    printk(KERN_WARNING "device %d released!\n", chan_num);
  }
}

void unicorn_video_timeout(unsigned long data)
{
  struct unicorn_timeout_data *timeout_data = (struct unicorn_timeout_data *)data;
  struct unicorn_dev *dev = timeout_data->dev;
  struct unicorn_dmaqueue *q = timeout_data->vidq;
  struct unicorn_buffer *buf;
  unsigned long flags;
  volatile unsigned long addr=0, addr1=0;
  int size = 0;
  dprintk_video(1, dev->name, "%s() channel:%d fifo_error:0x%x nb_timeout_fifo_full_error:0x%x\n", __func__, q->fh->channel,dev->fifo_full_error,
      dev->nb_timeout_fifo_full_error);

  spin_lock_irqsave(&dev->slock, flags);


  if(dev->fifo_full_error & (1<<q->fh->channel))
  {
    if( GET_TIMEOUT(dev->nb_timeout_fifo_full_error,q->fh->channel) < TIMEOUT_RETRY)
    {
      dev->nb_timeout_fifo_full_error = INC_TIMEOUT(dev->nb_timeout_fifo_full_error,q->fh->channel);
      mod_timer(&q->timeout, jiffies + BUFFER_TIMEOUT);

      if(dev->fps_limit[q->fh->channel]!=0)
      {
        dev->global_register->video[q->fh->channel].nb_us_inhibit = (1000/dev->fps_limit[q->fh->channel])*1000;
      }
      else
      {
        dev->global_register->video[q->fh->channel].nb_us_inhibit = 0;
      }

      dev->global_register->video_in_reset &= ~ (0x01 << q->fh->input) ;

      size = q->fh->width*q->fh->height*(q->fh->fmt->depth>>3)/UNICORN_DMA_BLOC_SIZE;

      dev->pcie_dma->dma[q->fh->channel].buff[0].size = size;
      dev->pcie_dma->dma[q->fh->channel].buff[1].size = size;

      addr = dev->pcie_dma->dma[q->fh->channel].buff[0].addr;
      addr1 = dev->pcie_dma->dma[q->fh->channel].buff[1].addr;

      dev->pcie_dma->dma[q->fh->channel].buff[0].addr = addr;
      dev->pcie_dma->dma[q->fh->channel].buff[1].addr = addr1;


      dev->pcie_dma->dma[q->fh->channel].ctrl |= DMA_CONTROL_AUTO_START;
      dev->pcie_dma->dma[q->fh->channel].ctrl |= DMA_CONTROL_START;
      dev->global_register->video[q->fh->channel].ctrl |= VIDEO_CONTROL_ENABLE;

      spin_unlock_irqrestore(&dev->slock, flags);
      return;
    }
    else
    {
      dev->fifo_full_error &= ~(1<<q->fh->channel);
      dev->nb_timeout_fifo_full_error = RESET_TIMEOUT(dev->nb_timeout_fifo_full_error,q->fh->channel);
    }
  }



  while (!list_empty(&q->active)) {
    buf = list_entry(q->active.next, struct unicorn_buffer, vb.queue);
    list_del(&buf->vb.queue);

    buf->vb.state = VIDEOBUF_ERROR;
    wake_up(&buf->vb.done);
  }
  while (!list_empty(&q->queued)) {
    buf = list_entry(q->queued.next, struct unicorn_buffer, vb.queue);
    list_del(&buf->vb.queue);

    buf->vb.state = VIDEOBUF_ERROR;
    wake_up(&buf->vb.done);
  }

  spin_unlock_irqrestore(&dev->slock, flags);
}


int unicorn_video_register(struct unicorn_dev *dev, int chan_num,
         struct video_device *video_template)
{
  int err;
  /* init video dma queues */
  spin_lock_init(&dev->slock);
  dev->timeout_data[chan_num].dev = dev;

  dev->timeout_data[chan_num].video =(struct video_in *) &dev->global_register->video[chan_num];
  dev->timeout_data[chan_num].dma = (struct dma_wr *) &dev->pcie_dma->dma[chan_num];
  dev->timeout_data[chan_num].vidq = &dev->vidq[chan_num];

  INIT_LIST_HEAD(&dev->vidq[chan_num].active);
  INIT_LIST_HEAD(&dev->vidq[chan_num].queued);
  dev->vidq[chan_num].timeout.function = unicorn_video_timeout;
  dev->vidq[chan_num].timeout.data =
      (unsigned long)&dev->timeout_data[chan_num];
  init_timer(&dev->vidq[chan_num].timeout);

  /* register v4l devices */
  dev->video_dev[chan_num] =
      unicorn_vdev_init(dev, dev->pci, video_template, "video");
  err =
      video_register_device(dev->video_dev[chan_num], VFL_TYPE_GRABBER,
          video_nr[dev->nr]);

  if (err < 0) {
    goto fail_unreg;
  }

//  init_controls(dev, chan_num);

  return 0;

  fail_unreg:
    unicorn_video_unregister(dev, chan_num);
  return err;
}

static int unicorn_attach_camera(struct unicorn_dev *dev)
{
  int i=0,try_i=0;

  dev->sensor[MIRE_VIDEO_INPUT] = NULL;


  for(i=0;i<(MAX_I2C_ADAPTER-1);i++)
  {
    for (try_i=0;try_i<3;try_i++)
    {
      struct v4l2_dbg_chip_ident chip;
      int ret;

      // try to find mt9m114
      dev->sensor[i] = v4l2_i2c_new_subdev(&dev->v4l2_dev, dev->i2c_adapter[i],
                                              "mt9m114", "mt9m114", 0x48, NULL);

      chip.ident = V4L2_IDENT_NONE;
      chip.match.type = V4L2_CHIP_MATCH_I2C_ADDR;
      chip.match.addr = 0x48;
      ret = v4l2_subdev_call(dev->sensor[i], core, g_chip_ident, &chip);
      if (ret)
      {
        if (ret == -ENODEV )
          dprintk_video(1, dev->name, "i2c subdev is null\n");
        if (ret == -ENOIOCTLCMD )
          printk(KERN_INFO "i2c core or chip ident is null\n");
      }
      else
      {
        break;
      }
      if (chip.ident != V4L2_IDENT_MT9M114) {
        dprintk_video(1, dev->name, "MT9M114: Unsupported sensor type 0x%x", chip.ident);
        dev->sensor[i] = NULL;
      }
#if 0
      // try to find ov7670
      if(dev->sensor[i]==NULL)
      {
        dev->sensor[i] = v4l2_i2c_new_subdev(&dev->v4l2_dev, dev->i2c_adapter[i],
                                                      "ov7670", "0v7670", 0x42, NULL);

        chip.ident = V4L2_IDENT_NONE;
        chip.match.type = V4L2_CHIP_MATCH_I2C_ADDR;
        chip.match.addr = 0x42;
        ret = v4l2_subdev_call(dev->sensor[i], core, g_chip_ident, &chip);
        printk(KERN_INFO "OV7670: type 0x%x", chip.ident);

      }
#endif
    }
  }
  return 0;
}

int unicorn_init_video(struct unicorn_dev *dev)
{
  int i=0;
  struct video_device *video_template[] = {
    &unicorn_video_template0,
            &unicorn_video_template1,
  };

  unicorn_attach_camera(dev);

  for (i = 0; i < VID_CHANNEL_NUM; i++) {
    if (unicorn_video_register(dev, i, video_template[i]) < 0) {
      printk(KERN_ERR
             "%s() Failed to register video channel %d\n",
             __func__, i);
      return -1;
    }
    dev->global_register->video[i].ctrl &= ~VIDEO_CONTROL_ENABLE;
    dev->pcie_dma->dma[i].ctrl |= DMA_CONTROL_RESET;
  }
  dev->global_register->video_in_reset = RESET_ALL_VIDEO_INPUT;
  return 0;
}

#endif
