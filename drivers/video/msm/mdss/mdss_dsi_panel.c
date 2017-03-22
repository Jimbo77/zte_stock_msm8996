/* Copyright (c) 2012-2016, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/qpnp/pin.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/leds.h>
#include <linux/qpnp/pwm.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/display_state.h>

#include "mdss_dsi.h"
#include "mdss_dba_utils.h"

#include <linux/proc_fs.h>
static struct proc_dir_entry * d_entry;
static struct proc_dir_entry *d_entry_frame_count;
static char  module_name[50]={"0"};
extern u32 zte_frame_count;/*pan*/
#define DT_CMD_HDR 6
#define MIN_REFRESH_RATE 48
#define DEFAULT_MDP_TRANSFER_TIME 14000

#define VSYNC_DELAY msecs_to_jiffies(17)


struct mutex zte_display_lock;
int zte_display_init=0;


DEFINE_LED_TRIGGER(bl_led_trigger);
#ifdef ZTE_SAMSUNG_ACL_HBM

static int old_bl_level = 0;
static void mdss_dsi_panel_cmds_send(struct mdss_dsi_ctrl_pdata *ctrl,
				     struct dsi_panel_cmds *pcmds, u32 flags);
static char led_acl_mode[2] = {0x55, 0x0};	/* DTYPE_DCS_WRITE1 */

static struct dsi_cmd_desc acl_cmd[] = {
	{	{DTYPE_DCS_WRITE1, 0, 0, 0, 1, sizeof(led_acl_mode)},
		led_acl_mode
	},
	{	{DTYPE_DCS_WRITE1, 0, 0, 0, 1, sizeof(led_acl_mode)},
		led_acl_mode
	},
	{	{DTYPE_DCS_WRITE1, 1, 0, 0, 1, sizeof(led_acl_mode)},
		led_acl_mode
	},
};
static char led_hbm_mode[2] = {0x53, 0x20};	/* DTYPE_DCS_WRITE1 */

static struct dsi_cmd_desc hbm_cmd[] = {
	{	{DTYPE_DCS_WRITE1, 0, 0, 0, 1, sizeof(led_hbm_mode)},
		led_hbm_mode
	},
	{	{DTYPE_DCS_WRITE1, 0, 0, 0, 1, sizeof(led_hbm_mode)},
		led_hbm_mode
	},
	{	{DTYPE_DCS_WRITE1, 1, 0, 0, 1, sizeof(led_hbm_mode)},
		led_hbm_mode
	},
};


static void mdss_dsi_panel_bklt_acl(struct mdss_dsi_ctrl_pdata *ctrl, int level)
{
	struct dcs_cmd_req cmdreq;
	struct mdss_panel_info *pinfo;

	if (ctrl == NULL)
		return;
	pinfo = &(ctrl->panel_data.panel_info);
	if (pinfo->dcs_cmd_by_left) {
		if (ctrl->ndx != DSI_CTRL_LEFT)
			return;
	}
	pr_info("%s: %d\n", __func__, level);
	led_acl_mode[1] = level;

	memset(&cmdreq, 0, sizeof(cmdreq));
	cmdreq.cmds = acl_cmd;
	cmdreq.cmds_cnt = 3;
	cmdreq.flags = CMD_REQ_COMMIT | CMD_CLK_CTRL | CMD_REQ_HS_MODE;
	cmdreq.rlen = 0;
	cmdreq.cb = NULL;

	mdss_dsi_cmdlist_put(ctrl, &cmdreq);

}
static void mdss_dsi_panel_bklt_hbm(struct mdss_dsi_ctrl_pdata *ctrl, int level)
{
	struct dcs_cmd_req cmdreq;
	struct mdss_panel_info *pinfo;

	if (ctrl == NULL)
		return;

	pinfo = &(ctrl->panel_data.panel_info);
	if (pinfo->dcs_cmd_by_left) {
		if (ctrl->ndx != DSI_CTRL_LEFT)
			return;
	}
	pr_info("%s: %d\n", __func__, level);
	if (level > 0)
		led_hbm_mode[1] = 0xe0;
	else
		led_hbm_mode[1] = 0x28;

	memset(&cmdreq, 0, sizeof(cmdreq));
	cmdreq.cmds = hbm_cmd;
	cmdreq.cmds_cnt = 3;
	cmdreq.flags = CMD_REQ_COMMIT | CMD_CLK_CTRL | CMD_REQ_HS_MODE;
	cmdreq.rlen = 0;
	cmdreq.cb = NULL;

	mdss_dsi_cmdlist_put(ctrl, &cmdreq);

}



int mdss_dsi_panel_acl(struct mdss_panel_data *pdata, int level)
{
	struct mdss_dsi_ctrl_pdata *ctrl = NULL;
	struct mdss_dsi_ctrl_pdata *sctrl = NULL;

	pr_info("%s: level == %d return\n", __func__, level);
	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl = container_of(pdata, struct mdss_dsi_ctrl_pdata,
			    panel_data);

	pr_info("%s: ctrl=%p ndx=%d\n", __func__, ctrl, ctrl->ndx);
	if (!mdss_dsi_sync_wait_enable(ctrl)) {
		mdss_dsi_panel_bklt_acl(ctrl, level);
		pr_info("%s: mdss_dsi_sync_wait_enable-\n", __func__);
		return 0;
	}
	/*
	 * DCS commands to update backlight are usually sent at
	 * the same time to both the controllers. However, if
	 * sync_wait is enabled, we need to ensure that the
	 * dcs commands are first sent to the non-trigger
	 * controller so that when the commands are triggered,
	 * both controllers receive it at the same time.
	 */
	sctrl = mdss_dsi_get_other_ctrl(ctrl);
	if (mdss_dsi_sync_wait_trigger(ctrl)) {
		if (sctrl) {
			mdss_dsi_panel_bklt_acl(sctrl, level);
		}
		mdss_dsi_panel_bklt_acl(ctrl, level);
	} else {
		mdss_dsi_panel_bklt_acl(ctrl, level);
		if (sctrl) {
			mdss_dsi_panel_bklt_acl(sctrl, level);
		}
	}


	pr_info("%s:-\n", __func__);
	return 0;
}

int mdss_dsi_panel_hbm(struct mdss_panel_data *pdata, int level)
{
	struct mdss_dsi_ctrl_pdata *ctrl = NULL;
	struct mdss_dsi_ctrl_pdata *sctrl = NULL;

	pr_info("%s: level == %d\n", __func__, level);
	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	if (old_bl_level != 255) {
		pr_err("%s: old_level = %d return\n", __func__, old_bl_level);
		return 0;
	}

	if (level == 1)
		mdss_dsi_panel_acl(pdata, 0);
	else if (old_bl_level != 255)
		mdss_dsi_panel_acl(pdata, 2);

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl = container_of(pdata, struct mdss_dsi_ctrl_pdata,
			    panel_data);

	pr_info("%s: ctrl=%p ndx=%d\n", __func__, ctrl, ctrl->ndx);

	if (!mdss_dsi_sync_wait_enable(ctrl)) {
		mdss_dsi_panel_bklt_hbm(ctrl, level);
		pr_info("%s: mdss_dsi_sync_wait_enable-\n", __func__);
		return 0;
	}
	/*
	 * DCS commands to update backlight are usually sent at
	 * the same time to both the controllers. However, if
	 * sync_wait is enabled, we need to ensure that the
	 * dcs commands are first sent to the non-trigger
	 * controller so that when the commands are triggered,
	 * both controllers receive it at the same time.
	 */
	sctrl = mdss_dsi_get_other_ctrl(ctrl);
	if (mdss_dsi_sync_wait_trigger(ctrl)) {
		if (sctrl) {
			mdss_dsi_panel_bklt_hbm(sctrl, level);
		}
		mdss_dsi_panel_bklt_hbm(ctrl, level);
	} else {
		mdss_dsi_panel_bklt_hbm(ctrl, level);
		if (sctrl) {
			mdss_dsi_panel_bklt_hbm(sctrl, level);
		}
	}


	pr_info("%s:-\n", __func__);
	return 0;
}

struct mdss_dsi_ctrl_pdata *g_ctrl_pdata;

static int panel_acl_proc_show(struct seq_file *m, void *v)
{

	seq_printf(m, "%d\n", g_ctrl_pdata->current_acl_level);

	return 0;
}

static int panel_acl_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, panel_acl_proc_show, NULL);
}



static ssize_t panel_acl_proc_write(struct file *file, const char __user *buffer,
				    size_t count, loff_t *pos)
{
	int ret;
	int rc;

	rc = kstrtoint_from_user(buffer, count, 0, &ret);
	if (rc)
		return rc;


	g_ctrl_pdata->current_acl_level = ret;


	pr_info("%s : current_acl_level = %d\n", __func__, g_ctrl_pdata->current_acl_level);

	mdss_dsi_panel_acl(&(g_ctrl_pdata->panel_data), g_ctrl_pdata->current_acl_level);

	return 1;
}


static const struct file_operations panel_acl_proc_fops = {
	.open		= panel_acl_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	       = single_release,
	.write		= panel_acl_proc_write,
};

static int panel_acl_proc_init(void)
{
	struct proc_dir_entry *res;

	res = proc_create("panel_acl_switch", S_IWUGO | S_IRUGO, NULL, &panel_acl_proc_fops);
	if (!res) {
		pr_err("failed to create /proc/panel_acl_switch\n");
		return -ENOMEM;
	}

	pr_info("created /proc/panel_acl_switch\n");
	return 0;
}

static int panel_hbm_proc_show(struct seq_file *m, void *v)
{

	seq_printf(m, "%d\n", g_ctrl_pdata->current_hbm_level);

	return 0;
}

static int panel_hbm_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, panel_hbm_proc_show, NULL);
}



static ssize_t panel_hbm_proc_write(struct file *file, const char __user *buffer,
				    size_t count, loff_t *pos)
{
	int ret;
	int rc;

	rc = kstrtoint_from_user(buffer, count, 0, &ret);
	if (rc)
		return rc;


	g_ctrl_pdata->current_hbm_level = ret;

	pr_err("%s : current_hbm_level = %d\n", __func__, g_ctrl_pdata->current_hbm_level);

	mdss_dsi_panel_hbm(&(g_ctrl_pdata->panel_data), g_ctrl_pdata->current_hbm_level);

	return 1;
}


static const struct file_operations panel_hbm_proc_fops = {
	.open		= panel_hbm_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= panel_hbm_proc_write,
};

static int panel_hbm_proc_init(void)
{
	struct proc_dir_entry *res;

	res = proc_create("panel_hbm_switch", S_IWUGO | S_IRUGO, NULL,  &panel_hbm_proc_fops);
	if (!res) {
		pr_err("failed to create /proc/panel_hbm_switch\n");
		return -ENOMEM;
	}

	pr_info("created /proc/panel_hbm_switch\n");

	return 0;
}

static int  samsung_panel_proc_init(struct mdss_dsi_ctrl_pdata *ctrl_pdata)
{
	int ret = -1;
	static int initial_flag = 0;

	if (ctrl_pdata == NULL)
		goto end;
	if (initial_flag == 1)
		goto end;
	else
		initial_flag = 1;
	g_ctrl_pdata = ctrl_pdata;
	ret = panel_acl_proc_init();
	ret = panel_hbm_proc_init();
end:
	return ret;
}


#endif

ssize_t mdss_dsi_panel_lcd_read_proc(struct file *file, char __user *page, size_t size, loff_t *ppos)
{
	int len = 0;
	printk("LCD %s:---enter---\n",__func__);

	/* ADB call again */
	if (*ppos)
		return 0;

	len = sprintf(page, "%s\n", module_name);
	*ppos += len;

	return len;
}
static const struct file_operations proc_ops = {
	.owner = THIS_MODULE,
	.read = mdss_dsi_panel_lcd_read_proc,
	.write = NULL,
};

static ssize_t mdss_dsi_panel_lcd_read_frame_count(struct file *file, char __user *page, size_t size, loff_t *ppos)
{
	int len = 0;

	/* ADB call again */
	if (*ppos)
		return 0;

	len = snprintf(NULL, 0, "%d\n", zte_frame_count);
	len = snprintf(page, len, "%d\n", zte_frame_count);
	*ppos += len;

	return len;
}

static const struct file_operations proc_ops_frame_count = {
		.owner = THIS_MODULE,
		.read = mdss_dsi_panel_lcd_read_frame_count,
		.write = NULL,
};

void  mdss_dsi_panel_lcd_proc(struct device_node *node)
{
	const char *panel_name;
	static int initial_flag =0;

	if(initial_flag==1)
		return ;
	else
		initial_flag = 1;

	d_entry=proc_create("msm_lcd", 0664, NULL, &proc_ops);
	if (d_entry == NULL)
		printk("LCD proc_create panel information failed!\n");

	d_entry_frame_count = proc_create("msm_frame_count", 0664, NULL, &proc_ops_frame_count);
	if (!d_entry_frame_count)
		pr_debug("LCD proc_create msm_frame_count failed!\n");

	panel_name = of_get_property(node,
		"qcom,mdss-dsi-panel-name", NULL);
	if (!panel_name){
		pr_info("LCD %s:%d, panel name not found!\n",
						__func__, __LINE__);
		strcpy(module_name,"0");
	}else{
		pr_info("LCD %s: Panel Name = %s\n", __func__, panel_name);
		strcpy(module_name,panel_name);
	}
}

bool display_on = true;
bool is_display_on()
{
	return display_on;
}

void mdss_dsi_panel_pwm_cfg(struct mdss_dsi_ctrl_pdata *ctrl)
{
	if (ctrl->pwm_pmi)
		return;

	ctrl->pwm_bl = pwm_request(ctrl->pwm_lpg_chan, "lcd-bklt");
	if (ctrl->pwm_bl == NULL || IS_ERR(ctrl->pwm_bl)) {
		pr_err("%s: Error: lpg_chan=%d pwm request failed",
				__func__, ctrl->pwm_lpg_chan);
	}
	ctrl->pwm_enabled = 0;
}

bool mdss_dsi_panel_pwm_enable(struct mdss_dsi_ctrl_pdata *ctrl)
{
	bool status = true;
	if (!ctrl->pwm_enabled)
		goto end;

	if (pwm_enable(ctrl->pwm_bl)) {
		pr_err("%s: pwm_enable() failed\n", __func__);
		status = false;
	}

	ctrl->pwm_enabled = 1;

end:
	return status;
}

static void mdss_dsi_panel_bklt_pwm(struct mdss_dsi_ctrl_pdata *ctrl, int level)
{
	int ret;
	u32 duty;
	u32 period_ns;

	if (ctrl->pwm_bl == NULL) {
		pr_err("%s: no PWM\n", __func__);
		return;
	}

	if (level == 0) {
		if (ctrl->pwm_enabled) {
			ret = pwm_config_us(ctrl->pwm_bl, level,
					ctrl->pwm_period);
			if (ret)
				pr_err("%s: pwm_config_us() failed err=%d.\n",
						__func__, ret);
			pwm_disable(ctrl->pwm_bl);
		}
		ctrl->pwm_enabled = 0;
		return;
	}

	duty = level * ctrl->pwm_period;
	duty /= ctrl->bklt_max;

	pr_debug("%s: bklt_ctrl=%d pwm_period=%d pwm_gpio=%d pwm_lpg_chan=%d\n",
			__func__, ctrl->bklt_ctrl, ctrl->pwm_period,
				ctrl->pwm_pmic_gpio, ctrl->pwm_lpg_chan);

	pr_debug("%s: ndx=%d level=%d duty=%d\n", __func__,
					ctrl->ndx, level, duty);

	if (ctrl->pwm_period >= USEC_PER_SEC) {
		ret = pwm_config_us(ctrl->pwm_bl, duty, ctrl->pwm_period);
		if (ret) {
			pr_err("%s: pwm_config_us() failed err=%d.\n",
					__func__, ret);
			return;
		}
	} else {
		period_ns = ctrl->pwm_period * NSEC_PER_USEC;
		ret = pwm_config(ctrl->pwm_bl,
				level * period_ns / ctrl->bklt_max,
				period_ns);
		if (ret) {
			pr_err("%s: pwm_config() failed err=%d.\n",
					__func__, ret);
			return;
		}
	}

	if (!ctrl->pwm_enabled) {
		ret = pwm_enable(ctrl->pwm_bl);
		if (ret)
			pr_err("%s: pwm_enable() failed err=%d\n", __func__,
				ret);
		ctrl->pwm_enabled = 1;
	}
}

static char dcs_cmd[2] = {0x54, 0x00}; /* DTYPE_DCS_READ */
static struct dsi_cmd_desc dcs_read_cmd = {
	{DTYPE_DCS_READ, 1, 0, 1, 5, sizeof(dcs_cmd)},
	dcs_cmd
};

int mdss_dsi_panel_cmd_read(struct mdss_dsi_ctrl_pdata *ctrl, char cmd0,
		char cmd1, void (*fxn)(int), char *rbuf, int len)
{
	struct dcs_cmd_req cmdreq;
	struct mdss_panel_info *pinfo;

	pinfo = &(ctrl->panel_data.panel_info);
	if (pinfo->dcs_cmd_by_left) {
		if (ctrl->ndx != DSI_CTRL_LEFT)
			return -EINVAL;
	}

	dcs_cmd[0] = cmd0;
	dcs_cmd[1] = cmd1;
	memset(&cmdreq, 0, sizeof(cmdreq));
	cmdreq.cmds = &dcs_read_cmd;
	cmdreq.cmds_cnt = 1;
//	cmdreq.flags = CMD_REQ_RX | CMD_REQ_COMMIT;
	cmdreq.flags = CMD_REQ_COMMIT | CMD_CLK_CTRL | CMD_REQ_RX;
	cmdreq.rlen = len;
	cmdreq.rbuf = rbuf;
	cmdreq.cb = fxn; /* call back */
	/*
	 * blocked here, until call back called
	 */

	return mdss_dsi_cmdlist_put(ctrl, &cmdreq);
}

static void mdss_dsi_panel_cmds_send(struct mdss_dsi_ctrl_pdata *ctrl,
			struct dsi_panel_cmds *pcmds, u32 flags)
{
	struct dcs_cmd_req cmdreq;
	struct mdss_panel_info *pinfo;

	pinfo = &(ctrl->panel_data.panel_info);
	if (pinfo->dcs_cmd_by_left) {
		if (ctrl->ndx != DSI_CTRL_LEFT)
			return;
	}

	memset(&cmdreq, 0, sizeof(cmdreq));
	cmdreq.cmds = pcmds->cmds;
	cmdreq.cmds_cnt = pcmds->cmd_cnt;
	cmdreq.flags = flags;

	/*Panel ON/Off commands should be sent in DSI Low Power Mode*/
	if (pcmds->link_state == DSI_LP_MODE)
		cmdreq.flags  |= CMD_REQ_LP_MODE;
	else if (pcmds->link_state == DSI_HS_MODE)
		cmdreq.flags |= CMD_REQ_HS_MODE;

	cmdreq.rlen = 0;
	cmdreq.cb = NULL;

	mdss_dsi_cmdlist_put(ctrl, &cmdreq);
}
static char power_on_flag=0;
/*samsung 5.5inch 2k panel ic defect,have to send 2 times for BL changing*/
static char led_pwm1[2] = {0x51, 0x0};	/* DTYPE_DCS_WRITE1 */
static char led_pwm2[2] = {0x53, 0x20};
static struct dsi_cmd_desc backlight_cmd[] = {
	{{DTYPE_DCS_WRITE1, 0, 0, 0, 0, sizeof(led_pwm1)},
	led_pwm1},
	{{DTYPE_DCS_WRITE1, 0, 0, 0, 0, sizeof(led_pwm1)},
	led_pwm1},
	{{DTYPE_DCS_WRITE1, 0, 0, 0, 0, sizeof(led_pwm1)},
	led_pwm1},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(led_pwm2)},
	led_pwm2,}
};

static void mdss_dsi_panel_bklt_dcs(struct mdss_dsi_ctrl_pdata *ctrl, int level)
{
	struct dcs_cmd_req cmdreq;
	struct mdss_panel_info *pinfo;
	int bl_level;
	pinfo = &(ctrl->panel_data.panel_info);

	mutex_lock(&zte_display_lock);
	
	if (pinfo->dcs_cmd_by_left) {
		if (ctrl->ndx != DSI_CTRL_LEFT)
		{
			mutex_unlock(&zte_display_lock);
			return;
		}
	}

#ifdef ZTE_BRIGHTNESS_CALIBRATION_NOT
	bl_level = level;
	pr_err("LCD %s: flag=%d level=%d -> new_level=%d\n", __func__, power_on_flag, level, bl_level);
#else
	if (level <11)
		bl_level = level;
	else if (level == 11)
		bl_level = 10;
	else
		bl_level = (15*level*level + 6059*level+29580)/10000;

	pr_err("LCD %s: flag=%d level=%d -> new_level=%d\n", __func__, power_on_flag, level, bl_level);
#endif

	led_pwm1[1] = (unsigned char)bl_level;

	if (power_on_flag == 1) {
		led_pwm2[1] = 0x20;
		power_on_flag = 2;
	} else if (power_on_flag == 2) {
		led_pwm2[1] = 0x20;
		power_on_flag = 0;
	} else
		led_pwm2[1] = 0x28;

	memset(&cmdreq, 0, sizeof(cmdreq));
	cmdreq.cmds = backlight_cmd;
	cmdreq.cmds_cnt = 4;
	cmdreq.flags = CMD_REQ_COMMIT | CMD_CLK_CTRL | CMD_REQ_HS_MODE;
	cmdreq.rlen = 0;
	cmdreq.cb = NULL;

	mdss_dsi_cmdlist_put(ctrl, &cmdreq);

	mutex_unlock(&zte_display_lock);
}

static char enable_R_AID[] = {0xF0, 0x5A, 0x5A};
static char R_AID_config_1[] = {0xB0, 0x0D};
static char R_AID_config_2[] = {0xB1, 0x08};
static char R_AID_disable_2[] = {0xB1, 0x80};
static char R_AID_config_3[] = {0xB0, 0x08};
//static char R_AID_config_4[] = {0xB1, 0x40, 0x06};
static char R_AID_config_4[] = {0xB1, 0x30, 0x56};
static char R_AID_disable_4[] = {0xB1, 0x20, 0x03};
static char R_AID_config_5[] = {0xCB, 0x10, 0x01, 0x80, 0x00, 0x00, 0x80, 0x60, 0x00,
		0x00, 0x06, 0x05, 0x00, 0x00, 0x00, 0x00, 0x0D, 0x00,
		0x15, 0x9A, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00,
		0x00, 0x00, 0x9D, 0x00, 0x00, 0xCA, 0x0A, 0x0A, 0x03,
		0xC5, 0x84, 0xCA, 0x0A, 0x0A, 0x0A, 0xCA, 0xCA, 0xCF,
		0xD1, 0xCD, 0xC3, 0xC5, 0xC4, 0x0A, 0x0A, 0x0A, 0x0A,
		0x0A, 0x0A, 0x00, 0x00, 0x0C, 0x01, 0x7B, 0x4D, 0x00,
		0x00, 0x10, 0x00};
static char R_AID_disable_5[] = {0xCB, 0x10, 0x01, 0x80, 0x00, 0x00, 0x80, 0x60, 0x00,
						0x00, 0x06, 0x05, 0x00, 0x00, 0x00, 0x06, 0x05, 0x00,
						0x15, 0x9A, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00,
						0x00, 0x00, 0x9D, 0x00, 0x00, 0xCA, 0x0A, 0x0A, 0x03,
						0xC5, 0x84, 0xCA, 0x0A, 0x0A, 0x0A, 0xCA, 0xCA, 0xCF,
						0xD1, 0x4D, 0xC3, 0xC5, 0xC4, 0x0A, 0x0A, 0x0A, 0x0A,
						0x0A, 0x0A, 0x00, 0x00, 0x0A, 0x01, 0x7B, 0x4D, 0x00,
						0x00, 0x08, 0x00};

#define VR_BRIGHTNESS 0x50
//#define VR_BRIGHTNESS 0x73
static char vr_vrightness[] = {0x51, VR_BRIGHTNESS};
static char R_AID_config_7[] = {0xF7, 0x03};
static char complete_R_AID[] = {0xF0, 0xA5, 0xA5};

#if 1
static char sleep_in[] = {0x10};
static char sleep_out[] = {0x11};
static char dispay_on[] = {0x29};
static char display_off[] = {0x28};

static struct dsi_cmd_desc sleep_in_cmd[] = {
	{{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(sleep_in)}, sleep_in},
};

static struct dsi_cmd_desc sleep_out_cmd[] = {
	{{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(sleep_out)}, sleep_out},
};

static struct dsi_cmd_desc display_on_cmd[] = {
	{{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(dispay_on)}, dispay_on},
};

static struct dsi_cmd_desc display_off_cmd[] = {
	{{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(display_off)}, display_off},
};

#endif

static struct dsi_cmd_desc R_AID_config_cmd[] = {
	{{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(enable_R_AID)}, enable_R_AID},
	{{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(enable_R_AID)}, enable_R_AID},
	{{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(R_AID_config_1)}, R_AID_config_1},
	{{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(R_AID_config_2)}, R_AID_config_2},
	{{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(R_AID_config_3)}, R_AID_config_3},
	{{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(R_AID_config_4)}, R_AID_config_4},
	{{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(R_AID_config_5)}, R_AID_config_5},
};

static struct dsi_cmd_desc R_AID_config_cmd_1[] = {
	{{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(vr_vrightness)}, vr_vrightness},
	{{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(vr_vrightness)}, vr_vrightness},
	{{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(R_AID_config_7)}, R_AID_config_7},
	{{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(complete_R_AID)}, complete_R_AID},
};

//120nit
//static char read_R_AID_offset_120[] = {0xC8, 0x00};

static char read_R_AID_offset_addr_120[] = {0xB0, 0x23};
static struct dsi_cmd_desc read_R_AID_offset_addr_120_cmd[] = {
	{{DTYPE_DCS_READ, 1, 0, 1, 1, sizeof(read_R_AID_offset_addr_120)},read_R_AID_offset_addr_120},
	{{DTYPE_DCS_READ, 1, 0, 1, 1, sizeof(read_R_AID_offset_addr_120)},read_R_AID_offset_addr_120},
};
static char read_R_AID_offset_120[] = {0xC8};
static struct dsi_cmd_desc read_R_AID_offset_120_cmd[] = {
	{{DTYPE_DCS_READ, 1, 0, 1, 1, sizeof(read_R_AID_offset_120)},read_R_AID_offset_120},
	{{DTYPE_DCS_READ, 1, 0, 1, 1, sizeof(read_R_AID_offset_120)},read_R_AID_offset_120},
	};

static char write_R_AID_offset_120[] = {0xC8,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,};
static struct dsi_cmd_desc write_R_AID_offset_120_cmd[] = {
	{{DTYPE_DCS_READ, 1, 0, 1, 5, sizeof(read_R_AID_offset_addr_120)},read_R_AID_offset_addr_120},
	{{DTYPE_DCS_READ, 1, 0, 1, 5, sizeof(read_R_AID_offset_addr_120)},read_R_AID_offset_addr_120},
	{{DTYPE_DCS_LWRITE, 1, 0, 1, 5, sizeof(write_R_AID_offset_120)},write_R_AID_offset_120},
};
static char offset_120_default[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,};
	static char offset_120[] = {0, 0, 0, 0, 0,
		0, 0, 0, 0, 0,
		0, 0, 0, 0, 0,
		0, 0, -16, -4,-2,
		-18,  4, -67, -34, 0};

//90nit
#if 1
static char read_R_AID_offset_addr_90[] = {0xB0, 0x43};
static struct dsi_cmd_desc read_R_AID_offset_addr_90_cmd[] = {
	{{DTYPE_DCS_READ, 1, 0, 1, 1, sizeof(read_R_AID_offset_addr_90)},read_R_AID_offset_addr_90},
	{{DTYPE_DCS_READ, 1, 0, 1, 1, sizeof(read_R_AID_offset_addr_90)},read_R_AID_offset_addr_90},
};

static char read_R_AID_offset_90[] = {0xC8};
static struct dsi_cmd_desc read_R_AID_offset_90_cmd[] = {
	{{DTYPE_DCS_READ, 1, 0, 1, 1, sizeof(read_R_AID_offset_90)},read_R_AID_offset_90},
	{{DTYPE_DCS_READ, 1, 0, 1, 1, sizeof(read_R_AID_offset_90)},read_R_AID_offset_90},
};

static char write_R_AID_offset_90[] = {0xC8,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,};
static struct dsi_cmd_desc write_R_AID_offset_90_cmd[] = {
	{{DTYPE_DCS_READ, 1, 0, 1, 5, sizeof(read_R_AID_offset_addr_90)},read_R_AID_offset_addr_90},
	{{DTYPE_DCS_READ, 1, 0, 1, 5, sizeof(read_R_AID_offset_addr_90)},read_R_AID_offset_addr_90},
	{{DTYPE_DCS_LWRITE, 1, 0, 1, 5, sizeof(write_R_AID_offset_90)},write_R_AID_offset_90},
};

static char offset_90_default[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,};

static char offset_90[] = {0, 0, 0,	0, 0,
	0,	0, 0, 0, 0,
	0,  0, 0, -4, -2,
	0, -2, -7, -1, -8,
	-8, -4, -55, -34, -28};
#endif

#if 0
//60nit
static char read_R_AID_offset_60[] = {0xC9};
static struct dsi_cmd_desc read_R_AID_offset_60_cmd[] = {
	{{DTYPE_DCS_READ, 1, 0, 1, 5, sizeof(read_R_AID_offset_60)},read_R_AID_offset_60},
	{{DTYPE_DCS_READ, 1, 0, 1, 5, sizeof(read_R_AID_offset_60)},read_R_AID_offset_60},
};

static char write_R_AID_offset_60[] = {0xC9,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,};
static struct dsi_cmd_desc write_R_AID_offset_60_cmd[] = {
	{{DTYPE_DCS_LWRITE, 1, 0, 1, 5, sizeof(write_R_AID_offset_60)},write_R_AID_offset_60},
	{{DTYPE_DCS_LWRITE, 1, 0, 1, 5, sizeof(write_R_AID_offset_60)},write_R_AID_offset_60},
};
static char offset_60_default[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,};
static char offset_60[] = {0, 2, 3,	 0, -1,
	-2,  1,  0,   0, -1,
	-1,  -3, -1,  -2,	-3,
	-1, -3, -5, -3, -8,
	-8, -4, -36, -23, -17};
#endif

/////////////////////disable command start///////////////////////////////////
static struct dsi_cmd_desc R_AID_disable_cmd[] ={
	{{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(enable_R_AID)}, enable_R_AID},
	{{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(enable_R_AID)}, enable_R_AID},
	{{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(R_AID_config_1)}, R_AID_config_1},
	{{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(R_AID_disable_2)}, R_AID_disable_2},
	{{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(R_AID_config_3)}, R_AID_config_3},
	{{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(R_AID_disable_4)}, R_AID_disable_4},
	{{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(R_AID_disable_5)}, R_AID_disable_5},
};
static struct dsi_cmd_desc R_AID_disable_120_cmd[] ={
	{{DTYPE_DCS_READ, 1, 0, 1, 1, sizeof(read_R_AID_offset_addr_120)},read_R_AID_offset_addr_120},
	{{DTYPE_DCS_READ, 1, 0, 1, 1, sizeof(read_R_AID_offset_addr_120)},read_R_AID_offset_addr_120},
	{{DTYPE_DCS_LWRITE, 1, 0, 0, 1, sizeof(write_R_AID_offset_120)}, write_R_AID_offset_120},
};
static struct dsi_cmd_desc R_AID_disable_90_cmd[] ={
	{{DTYPE_DCS_READ, 1, 0, 1, 1, sizeof(read_R_AID_offset_addr_90)},read_R_AID_offset_addr_90},
	{{DTYPE_DCS_READ, 1, 0, 1, 1, sizeof(read_R_AID_offset_addr_90)},read_R_AID_offset_addr_90},
	{{DTYPE_DCS_LWRITE, 1, 0, 0, 1, sizeof(write_R_AID_offset_90)}, write_R_AID_offset_90},
};
#if 0
static struct dsi_cmd_desc R_AID_disable_60_cmd[] ={
	{{DTYPE_DCS_LWRITE, 1, 0, 1, 5, sizeof(write_R_AID_offset_60)},write_R_AID_offset_60},
	{{DTYPE_DCS_LWRITE, 1, 0, 1, 5, sizeof(write_R_AID_offset_60)},write_R_AID_offset_60},
};
#endif
static struct dsi_cmd_desc R_AID_disable_complete_cmd[] ={
	//{{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(R_AID_disable_6)}, R_AID_disable_6},
	//{{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(R_AID_disable_6)}, R_AID_disable_6},
	{{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(R_AID_config_7)}, R_AID_config_7},
	{{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(R_AID_config_7)}, R_AID_config_7},
	{{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(complete_R_AID)}, complete_R_AID},
};
/////////////////////disable command end///////////////////////////////////

int mdss_dsi_read_R_AID_offset(struct mdss_dsi_ctrl_pdata *ctrl, struct dsi_cmd_desc* read_cmd,
	char *rbuf, char* offset, char* result, int len)
{
	struct dcs_cmd_req cmdreq;
	struct mdss_panel_info *pinfo;
	int ret;
	int index;

	pinfo = &(ctrl->panel_data.panel_info);
	if (pinfo->dcs_cmd_by_left) {
		if (ctrl->ndx != DSI_CTRL_LEFT)
		{
			//printk("jiangfeng %s, line %d, return!!!\n", __func__, __LINE__);
			return -EINVAL;
		}
	}

	memset(&cmdreq, 0, sizeof(cmdreq));
	cmdreq.cmds = read_cmd;
	cmdreq.cmds_cnt = 2;
//	cmdreq.flags = CMD_REQ_RX | CMD_REQ_COMMIT;
    cmdreq.flags = CMD_REQ_COMMIT | CMD_CLK_CTRL | CMD_REQ_RX;
	cmdreq.rlen = len;
	cmdreq.rbuf = rbuf;
	cmdreq.cb = NULL;

	ret = mdss_dsi_cmdlist_put(ctrl, &cmdreq);
	if(ret)
		return ret;

#if 0
	for(index = 0;index < len; index++)
	{
		printk("jiangfeng %s, line %d, index %d, value %d\n", __func__, __LINE__, index, rbuf[index]);
	}
#endif

	for(index = 0;index < len; index++)
	{
		result[index] = rbuf[index] - offset[index];
	}

#if 0
	for(index = 0;index < len; index++)
	{
		printk("jiangfeng %s, line %d, index %d, value %d\n", __func__, __LINE__, index, result[index]);
	}
#endif
	return 0;
}

static void mdss_dsi_enable_R_AID(struct mdss_dsi_ctrl_pdata *ctrl, int enable)
{
	int ret;
	struct mdss_panel_info *pinfo;

	struct dcs_cmd_req cmdreq_config;
	struct dcs_cmd_req cmdreq_config_complete;

	struct dcs_cmd_req cmdreq_read_offset_addr_120;
	struct dcs_cmd_req cmdreq_write_offset_120;
	struct dcs_cmd_req cmdreq_read_offset_addr_90;
	struct dcs_cmd_req cmdreq_write_offset_90;
	//struct dcs_cmd_req cmdreq_write_offset_60;

	struct dcs_cmd_req cmdreq_disable;
	struct dcs_cmd_req cmdreq_disable_complete;
	struct dcs_cmd_req cmdreq_disable_120;
	struct dcs_cmd_req cmdreq_disable_90;
	//struct dcs_cmd_req cmdreq_disable_60;

	static int inited_120 = 0;
	static int inited_90 = 0;
	
	//static int inited_60 = 0;

	pinfo = &(ctrl->panel_data.panel_info);

	if (pinfo->dcs_cmd_by_left) {
		if (ctrl->ndx != DSI_CTRL_LEFT)
		{
		    printk("jiangfeng %s, line %d, ret %d, return!!!\n", __func__, __LINE__, ret);
			return;
		}
	}

	if(enable)
	{
		memset(&cmdreq_config, 0, sizeof(cmdreq_config));
		cmdreq_config.cmds_cnt = 7;
		cmdreq_config.flags = CMD_REQ_COMMIT | CMD_CLK_CTRL;
		cmdreq_config.rlen = 0;
		cmdreq_config.cb = NULL;

		cmdreq_config.cmds = R_AID_config_cmd;
		ret = mdss_dsi_cmdlist_put(ctrl, &cmdreq_config);
		printk("jiangfeng %s, line %d, ret %d\n", __func__, __LINE__, ret);

		//120nit
		if(!inited_120)
		{
			memset(&cmdreq_read_offset_addr_120, 0, sizeof(cmdreq_read_offset_addr_120));
			cmdreq_read_offset_addr_120.cmds_cnt = 2;
			cmdreq_read_offset_addr_120.flags = CMD_REQ_COMMIT | CMD_CLK_CTRL;
			cmdreq_read_offset_addr_120.rlen = 0;
			cmdreq_read_offset_addr_120.cb = NULL;
			cmdreq_read_offset_addr_120.cmds = read_R_AID_offset_addr_120_cmd;
			ret = mdss_dsi_cmdlist_put(ctrl, &cmdreq_read_offset_addr_120);

			mdss_dsi_read_R_AID_offset(ctrl,read_R_AID_offset_120_cmd, offset_120_default, offset_120, write_R_AID_offset_120 +1, 25);
			inited_120 = 1;
			if(ret)
			{
				printk("jiangfeng %s, line %d, ret %d, return!!!\n", __func__, __LINE__, ret);
				return;
			}
		}

		memset(&cmdreq_write_offset_120, 0, sizeof(cmdreq_write_offset_120));
		cmdreq_write_offset_120.cmds_cnt = 3;
		cmdreq_write_offset_120.flags = CMD_REQ_COMMIT | CMD_CLK_CTRL;
		cmdreq_write_offset_120.rlen = 0;
		cmdreq_write_offset_120.cb = NULL;
		cmdreq_write_offset_120.cmds = write_R_AID_offset_120_cmd;
		ret = mdss_dsi_cmdlist_put(ctrl, &cmdreq_write_offset_120);

		printk("jiangfeng %s, line %d, ret %d\n", __func__, __LINE__, ret);

		//90nit
#if 1
		if(!inited_90)
		{
			memset(&cmdreq_read_offset_addr_90, 0, sizeof(cmdreq_read_offset_addr_90));
			cmdreq_read_offset_addr_90.cmds_cnt = 2;
			cmdreq_read_offset_addr_90.flags = CMD_REQ_COMMIT | CMD_CLK_CTRL;
			cmdreq_read_offset_addr_90.rlen = 0;
			cmdreq_read_offset_addr_90.cb = NULL;
			cmdreq_read_offset_addr_90.cmds = read_R_AID_offset_addr_90_cmd;
			ret = mdss_dsi_cmdlist_put(ctrl, &cmdreq_read_offset_addr_90);
			printk("jiangfeng %s, line %d, ret %d\n", __func__, __LINE__, ret);

			mdss_dsi_read_R_AID_offset(ctrl,read_R_AID_offset_90_cmd, offset_90_default, offset_90, write_R_AID_offset_90 + 1, 25);
			inited_90 = 1;
			if(ret)
			{
				printk("jiangfeng %s, line %d, ret %d, return!!!\n", __func__, __LINE__, ret);
				return;
			}
		}

		memset(&cmdreq_write_offset_90, 0, sizeof(cmdreq_write_offset_90));
		cmdreq_write_offset_90.cmds_cnt = 3;
		cmdreq_write_offset_90.flags = CMD_REQ_COMMIT | CMD_CLK_CTRL;
		cmdreq_write_offset_90.rlen = 0;
		cmdreq_write_offset_90.cb = NULL;
		cmdreq_write_offset_90.cmds = write_R_AID_offset_90_cmd;
		ret = mdss_dsi_cmdlist_put(ctrl, &cmdreq_write_offset_90);

		printk("jiangfeng %s, line %d, ret %d\n", __func__, __LINE__, ret);
#endif
#if 0
		//60nit
		if(!inited_60)
		{
			mdss_dsi_read_R_AID_offset(ctrl,read_R_AID_offset_60_cmd, offset_60_default, offset_60, write_R_AID_offset_60 +1, 25);
			inited_60 = 1;
		}

		memset(&cmdreq_write_offset_60, 0, sizeof(cmdreq_write_offset_60));
		cmdreq_write_offset_60.cmds_cnt = 2;
		cmdreq_write_offset_60.flags = CMD_REQ_COMMIT | CMD_CLK_CTRL;
		cmdreq_write_offset_60.rlen = 0;
		cmdreq_write_offset_60.cb = NULL;
		cmdreq_write_offset_60.cmds = write_R_AID_offset_60_cmd;
		ret = mdss_dsi_cmdlist_put(ctrl, &cmdreq_write_offset_60);

		//printk("jiangfeng %s, line %d, ret %d\n", __func__, __LINE__, ret);
#endif

		//complete config
		memset(&cmdreq_config_complete, 0, sizeof(cmdreq_config_complete));
		cmdreq_config_complete.cmds_cnt = 4;
		cmdreq_config_complete.flags = CMD_REQ_COMMIT | CMD_CLK_CTRL;
		cmdreq_config_complete.rlen = 0;
		cmdreq_config_complete.cb = NULL;

		cmdreq_config_complete.cmds = R_AID_config_cmd_1;
		ret = mdss_dsi_cmdlist_put(ctrl, &cmdreq_config_complete);
		printk("jiangfeng %s, line %d, ret %d\n", __func__, __LINE__, ret);
		
	}
	else if(ctrl->ctrl_state & CTRL_STATE_MDP_ACTIVE)
	{
		memset(&cmdreq_disable, 0, sizeof(cmdreq_disable));
		cmdreq_disable.cmds_cnt = 7;
		cmdreq_disable.flags = CMD_REQ_COMMIT | CMD_CLK_CTRL;
		cmdreq_disable.rlen = 0;
		cmdreq_disable.cb = NULL;

		cmdreq_disable.cmds = R_AID_disable_cmd;
		ret = mdss_dsi_cmdlist_put(ctrl, &cmdreq_disable);

		printk("jiangfeng %s, line %d, ret %d\n", __func__, __LINE__, ret);

		//120nit
		memcpy(write_R_AID_offset_120 + 1, offset_120_default, 25);

		memset(&cmdreq_disable_120, 0, sizeof(cmdreq_disable_120));
		cmdreq_disable_120.cmds_cnt = 3;
		cmdreq_disable_120.flags = CMD_REQ_COMMIT | CMD_CLK_CTRL;
		cmdreq_disable_120.rlen = 0;
		cmdreq_disable_120.cb = NULL;

		cmdreq_disable_120.cmds = R_AID_disable_120_cmd;
		ret = mdss_dsi_cmdlist_put(ctrl, &cmdreq_disable_120);
		printk("jiangfeng %s, line %d, ret %d\n", __func__, __LINE__, ret);

		//90nit
		memcpy(write_R_AID_offset_90 + 1, offset_90_default, 25);

		memset(&cmdreq_disable_90, 0, sizeof(cmdreq_disable_90));
		cmdreq_disable_90.cmds_cnt = 3;
		cmdreq_disable_90.flags = CMD_REQ_COMMIT | CMD_CLK_CTRL;
		cmdreq_disable_90.rlen = 0;
		cmdreq_disable_90.cb = NULL;

		cmdreq_disable_90.cmds = R_AID_disable_90_cmd;
		ret = mdss_dsi_cmdlist_put(ctrl, &cmdreq_disable_90);
		printk("jiangfeng %s, line %d, ret %d\n", __func__, __LINE__, ret);
#if 0
		memcpy(write_R_AID_offset_60 + 1, offset_60_default, 25);

		memset(&cmdreq_disable_60, 0, sizeof(cmdreq_disable_60));
		cmdreq_disable_60.cmds_cnt = 2;
		cmdreq_disable_60.flags = CMD_REQ_COMMIT | CMD_CLK_CTRL;
		cmdreq_disable_60.rlen = 0;
		cmdreq_disable_60.cb = NULL;

		cmdreq_disable_60.cmds = R_AID_disable_60_cmd;
		ret = mdss_dsi_cmdlist_put(ctrl, &cmdreq_disable_60);
#endif

		memset(&cmdreq_disable_complete, 0, sizeof(cmdreq_disable_complete));
		cmdreq_disable_complete.cmds_cnt = 3;
		cmdreq_disable_complete.flags = CMD_REQ_COMMIT | CMD_CLK_CTRL;
		cmdreq_disable_complete.rlen = 0;
		cmdreq_disable_complete.cb = NULL;

		cmdreq_disable_complete.cmds = R_AID_disable_complete_cmd;
		ret = mdss_dsi_cmdlist_put(ctrl, &cmdreq_disable_complete);
		printk("jiangfeng %s, line %d, ret %d\n", __func__, __LINE__, ret);
#if 0
		inited_120 = 0;
		inited_90 = 0;
		inited_60 = 0;
#endif
	}
    else
    {
        printk("jiangfeng %s, line %d, ret %d\n", __func__, __LINE__, ret);
    }

	
	printk("jiangfeng %s, line %d, ret %d, enable %d\n", __func__, __LINE__, ret, enable);
}


int g_vr_mode=0;
int g_vr_cnt=0;
void zte_wake_up_display(int enable);

static void zte_mdss_dsi_panel_enable_R_AID(struct mdss_panel_data *pdata, bool enable)
{
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	struct mdss_dsi_ctrl_pdata *sctrl = NULL;
	int status=0;

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	if (!mdss_dsi_sync_wait_enable(ctrl_pdata)) {
		mdss_dsi_enable_R_AID(ctrl_pdata, enable);
		return;
	}

	sctrl = mdss_dsi_get_other_ctrl(ctrl_pdata);
	if (mdss_dsi_sync_wait_trigger(ctrl_pdata)) {
		status|=0x01;
		if (sctrl)
			{
			status|=0x02;
			mdss_dsi_enable_R_AID(sctrl, enable);
			}
		mdss_dsi_enable_R_AID(ctrl_pdata, enable);
	} else {
	    status|=0x04;
		mdss_dsi_enable_R_AID(ctrl_pdata, enable);
		if (sctrl)
			{
			status|=0x08;
			mdss_dsi_enable_R_AID(sctrl, enable);
			}
	}
	
}


static void mdss_dsi_panel_enable_R_AID(struct mdss_panel_data *pdata, bool enable)
{
   
   struct dcs_cmd_req cmdreq_config;
   struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;

   int ret;

   ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

    mutex_lock(&zte_display_lock);
	zte_wake_up_display(0);
	
    g_vr_mode=1;
	g_vr_cnt++;

    printk("zte_display +++++++++: %s, vrmode=%d g_vr_cnt=%d\n", __func__, enable,g_vr_cnt);


    memset(&cmdreq_config, 0, sizeof(cmdreq_config));
	cmdreq_config.cmds_cnt = 1;
	cmdreq_config.flags = CMD_REQ_COMMIT | CMD_CLK_CTRL;
	cmdreq_config.rlen = 0;
	cmdreq_config.cb = NULL;

	cmdreq_config.cmds = display_off_cmd;
	ret = mdss_dsi_cmdlist_put(ctrl_pdata, &cmdreq_config);

    msleep(50);

	memset(&cmdreq_config, 0, sizeof(cmdreq_config));
	cmdreq_config.cmds_cnt = 1;
	cmdreq_config.flags = CMD_REQ_COMMIT | CMD_CLK_CTRL;
	cmdreq_config.rlen = 0;
	cmdreq_config.cb = NULL;

	cmdreq_config.cmds = sleep_in_cmd;
	ret = mdss_dsi_cmdlist_put(ctrl_pdata, &cmdreq_config);

	msleep(150);

	memset(&cmdreq_config, 0, sizeof(cmdreq_config));
	cmdreq_config.cmds_cnt = 1;
	cmdreq_config.flags = CMD_REQ_COMMIT | CMD_CLK_CTRL;
	cmdreq_config.rlen = 0;
	cmdreq_config.cb = NULL;

	cmdreq_config.cmds = sleep_out_cmd;
	ret = mdss_dsi_cmdlist_put(ctrl_pdata, &cmdreq_config);

    msleep(20);

	zte_mdss_dsi_panel_enable_R_AID(pdata,enable);

	msleep(150);

    memset(&cmdreq_config, 0, sizeof(cmdreq_config));
	cmdreq_config.cmds_cnt = 1;
	cmdreq_config.flags = CMD_REQ_COMMIT | CMD_CLK_CTRL;
	cmdreq_config.rlen = 0;
	cmdreq_config.cb = NULL;

	cmdreq_config.cmds = display_on_cmd;
	ret = mdss_dsi_cmdlist_put(ctrl_pdata, &cmdreq_config);

	 g_vr_mode=0;
     zte_wake_up_display(1);
	 
	 printk("zte_display ---------: %s, vrmode=%d\n", __func__, enable);
	 mutex_unlock(&zte_display_lock);
	 
}


static int mdss_dsi_request_gpios(struct mdss_dsi_ctrl_pdata *ctrl_pdata)
{
	int rc = 0;

	if (gpio_is_valid(ctrl_pdata->disp_en_gpio)) {
		rc = gpio_request(ctrl_pdata->disp_en_gpio,
						"disp_enable");
		if (rc) {
			pr_err("request disp_en gpio failed, rc=%d\n",
				       rc);
			goto disp_en_gpio_err;
		}
	}
	rc = gpio_request(ctrl_pdata->rst_gpio, "disp_rst_n");
	if (rc) {
		pr_err("request reset gpio failed, rc=%d\n",
			rc);
		goto rst_gpio_err;
	}
	if (gpio_is_valid(ctrl_pdata->bklt_en_gpio)) {
		rc = gpio_request(ctrl_pdata->bklt_en_gpio,
						"bklt_enable");
		if (rc) {
			pr_err("request bklt gpio failed, rc=%d\n",
				       rc);
			goto bklt_en_gpio_err;
		}
	}
	if (gpio_is_valid(ctrl_pdata->mode_gpio)) {
		rc = gpio_request(ctrl_pdata->mode_gpio, "panel_mode");
		if (rc) {
			pr_err("request panel mode gpio failed,rc=%d\n",
								rc);
			goto mode_gpio_err;
		}
	}
	return rc;

mode_gpio_err:
	if (gpio_is_valid(ctrl_pdata->bklt_en_gpio))
		gpio_free(ctrl_pdata->bklt_en_gpio);
bklt_en_gpio_err:
	gpio_free(ctrl_pdata->rst_gpio);
rst_gpio_err:
	if (gpio_is_valid(ctrl_pdata->disp_en_gpio))
		gpio_free(ctrl_pdata->disp_en_gpio);
disp_en_gpio_err:
	return rc;
}

int mdss_dsi_panel_reset(struct mdss_panel_data *pdata, int enable)
{
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	struct mdss_panel_info *pinfo = NULL;
	int i, rc = 0;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	pinfo = &(ctrl_pdata->panel_data.panel_info);
	if ((mdss_dsi_is_right_ctrl(ctrl_pdata) &&
		mdss_dsi_is_hw_config_split(ctrl_pdata->shared_data)) ||
			pinfo->is_dba_panel) {
		pr_debug("%s:%d, right ctrl gpio configuration not needed\n",
			__func__, __LINE__);
		return rc;
	}

	if (!gpio_is_valid(ctrl_pdata->disp_en_gpio)) {
		pr_debug("%s:%d, reset line not configured\n",
			   __func__, __LINE__);
	}

	if (!gpio_is_valid(ctrl_pdata->rst_gpio)) {
		pr_debug("%s:%d, reset line not configured\n",
			   __func__, __LINE__);
		return rc;
	}

	pr_debug("%s: enable = %d\n", __func__, enable);

	if (enable) {
		rc = mdss_dsi_request_gpios(ctrl_pdata);
		if (rc) {
			pr_err("gpio request failed\n");
			return rc;
		}
		if (!pinfo->cont_splash_enabled) {
			if (gpio_is_valid(ctrl_pdata->disp_en_gpio)) {
				rc = gpio_direction_output(
					ctrl_pdata->disp_en_gpio, 1);
				if (rc) {
					pr_err("%s: unable to set dir for en gpio\n",
						__func__);
					goto exit;
				}
			}

			if (pdata->panel_info.rst_seq_len) {
				rc = gpio_direction_output(ctrl_pdata->rst_gpio,
					pdata->panel_info.rst_seq[0]);
				if (rc) {
					pr_err("%s: unable to set dir for rst gpio\n",
						__func__);
					goto exit;
				}
			}

			for (i = 0; i < pdata->panel_info.rst_seq_len; ++i) {
				gpio_set_value((ctrl_pdata->rst_gpio),
					pdata->panel_info.rst_seq[i]);
				if (pdata->panel_info.rst_seq[++i])
					usleep_range(pinfo->rst_seq[i] * 1000, pinfo->rst_seq[i] * 1000);
			}

			if (gpio_is_valid(ctrl_pdata->bklt_en_gpio)) {
				rc = gpio_direction_output(
					ctrl_pdata->bklt_en_gpio, 1);
				if (rc) {
					pr_err("%s: unable to set dir for bklt gpio\n",
						__func__);
					goto exit;
				}
			}
		}

		if (gpio_is_valid(ctrl_pdata->mode_gpio)) {
			bool out = false;

			if (pinfo->mode_gpio_state == MODE_GPIO_HIGH)
				out = true;
			else if (pinfo->mode_gpio_state == MODE_GPIO_LOW)
				out = false;

			rc = gpio_direction_output(ctrl_pdata->mode_gpio, out);
			if (rc) {
				pr_err("%s: unable to set dir for mode gpio\n",
					__func__);
				goto exit;
			}
		}
		if (ctrl_pdata->ctrl_state & CTRL_STATE_PANEL_INIT) {
			pr_debug("%s: Panel Not properly turned OFF\n",
						__func__);
			ctrl_pdata->ctrl_state &= ~CTRL_STATE_PANEL_INIT;
			pr_debug("%s: Reset panel done\n", __func__);
		}
	} else {
		if (gpio_is_valid(ctrl_pdata->bklt_en_gpio)) {
			gpio_set_value((ctrl_pdata->bklt_en_gpio), 0);
			gpio_free(ctrl_pdata->bklt_en_gpio);
		}
		if (gpio_is_valid(ctrl_pdata->disp_en_gpio)) {
			gpio_set_value((ctrl_pdata->disp_en_gpio), 0);
			gpio_free(ctrl_pdata->disp_en_gpio);
		}
		gpio_set_value((ctrl_pdata->rst_gpio), 0);
		gpio_free(ctrl_pdata->rst_gpio);
		if (gpio_is_valid(ctrl_pdata->mode_gpio))
			gpio_free(ctrl_pdata->mode_gpio);
	}

exit:
	return rc;
}

/**
 * mdss_dsi_roi_merge() -  merge two roi into single roi
 *
 * Function used by partial update with only one dsi intf take 2A/2B
 * (column/page) dcs commands.
 */
static int mdss_dsi_roi_merge(struct mdss_dsi_ctrl_pdata *ctrl,
					struct mdss_rect *roi)
{
	struct mdss_panel_info *l_pinfo;
	struct mdss_rect *l_roi;
	struct mdss_rect *r_roi;
	struct mdss_dsi_ctrl_pdata *other = NULL;
	int ans = 0;

	if (ctrl->ndx == DSI_CTRL_LEFT) {
		other = mdss_dsi_get_ctrl_by_index(DSI_CTRL_RIGHT);
		if (!other)
			return ans;
		l_pinfo = &(ctrl->panel_data.panel_info);
		l_roi = &(ctrl->panel_data.panel_info.roi);
		r_roi = &(other->panel_data.panel_info.roi);
	} else  {
		other = mdss_dsi_get_ctrl_by_index(DSI_CTRL_LEFT);
		if (!other)
			return ans;
		l_pinfo = &(other->panel_data.panel_info);
		l_roi = &(other->panel_data.panel_info.roi);
		r_roi = &(ctrl->panel_data.panel_info.roi);
	}

	if (l_roi->w == 0 && l_roi->h == 0) {
		/* right only */
		*roi = *r_roi;
		roi->x += l_pinfo->xres;/* add left full width to x-offset */
	} else {
		/* left only and left+righ */
		*roi = *l_roi;
		roi->w +=  r_roi->w; /* add right width */
		ans = 1;
	}

	return ans;
}

static char key_enable[] = {0xf0, 0x5a, 0x5a};
static char caset[] = {0x2a, 0x00, 0x00, 0x03, 0x00};	/* DTYPE_DCS_LWRITE */
static char paset[] = {0x2b, 0x00, 0x00, 0x05, 0x00};	/* DTYPE_DCS_LWRITE */
static char key_disable[] = {0xf0, 0xa5, 0xa5};

/* pack into one frame before sent */
static struct dsi_cmd_desc set_col_page_addr_cmd[] = {
	{{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(key_enable)}, key_enable},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(caset)}, caset},	/* packed */
	{{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(paset)}, paset},
	{{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(key_disable)}, key_disable},
};

static void mdss_dsi_send_col_page_addr(struct mdss_dsi_ctrl_pdata *ctrl,
				struct mdss_rect *roi, int unicast)
{
	struct dcs_cmd_req cmdreq;

	caset[1] = (((roi->x) & 0xFF00) >> 8);
	caset[2] = (((roi->x) & 0xFF));
	caset[3] = (((roi->x - 1 + roi->w) & 0xFF00) >> 8);
	caset[4] = (((roi->x - 1 + roi->w) & 0xFF));
	set_col_page_addr_cmd[1].payload = caset;

	paset[1] = (((roi->y) & 0xFF00) >> 8);
	paset[2] = (((roi->y) & 0xFF));
	paset[3] = (((roi->y - 1 + roi->h) & 0xFF00) >> 8);
	paset[4] = (((roi->y - 1 + roi->h) & 0xFF));
	set_col_page_addr_cmd[2].payload = paset;

	memset(&cmdreq, 0, sizeof(cmdreq));
	cmdreq.cmds_cnt = 4;
	cmdreq.flags = CMD_REQ_COMMIT | CMD_CLK_CTRL;
	if (unicast)
		cmdreq.flags |= CMD_REQ_UNICAST;
	cmdreq.rlen = 0;
	cmdreq.cb = NULL;

	cmdreq.cmds = set_col_page_addr_cmd;
	mdss_dsi_cmdlist_put(ctrl, &cmdreq);
}

static int mdss_dsi_set_col_page_addr(struct mdss_panel_data *pdata,
		bool force_send)
{
	struct mdss_panel_info *pinfo;
	struct mdss_rect roi = {0};
	struct mdss_rect *p_roi;
	struct mdss_rect *c_roi;
	struct mdss_dsi_ctrl_pdata *ctrl = NULL;
	struct mdss_dsi_ctrl_pdata *other = NULL;
	int left_or_both = 0;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	pinfo = &pdata->panel_info;
	p_roi = &pinfo->roi;

	/*
	 * to avoid keep sending same col_page info to panel,
	 * if roi_merge enabled, the roi of left ctrl is used
	 * to compare against new merged roi and saved new
	 * merged roi to it after comparing.
	 * if roi_merge disabled, then the calling ctrl's roi
	 * and pinfo's roi are used to compare.
	 */
	if (pinfo->partial_update_roi_merge) {
		left_or_both = mdss_dsi_roi_merge(ctrl, &roi);
		other = mdss_dsi_get_ctrl_by_index(DSI_CTRL_LEFT);
		c_roi = &other->roi;
	} else {
		c_roi = &ctrl->roi;
		roi = *p_roi;
	}

	/* roi had changed, do col_page update */
	if (force_send || !mdss_rect_cmp(c_roi, &roi)) {
		pr_debug("%s: ndx=%d x=%d y=%d w=%d h=%d\n",
				__func__, ctrl->ndx, p_roi->x,
				p_roi->y, p_roi->w, p_roi->h);

		*c_roi = roi; /* keep to ctrl */
		if (c_roi->w == 0 || c_roi->h == 0) {
			/* no new frame update */
			pr_debug("%s: ctrl=%d, no partial roi set\n",
						__func__, ctrl->ndx);
			return 0;
		}

		if (pinfo->dcs_cmd_by_left) {
			if (left_or_both && ctrl->ndx == DSI_CTRL_RIGHT) {
				/* 2A/2B sent by left already */
				return 0;
			}
		}

		if (!mdss_dsi_sync_wait_enable(ctrl)) {
			if (pinfo->dcs_cmd_by_left)
				ctrl = mdss_dsi_get_ctrl_by_index(
							DSI_CTRL_LEFT);
			mdss_dsi_send_col_page_addr(ctrl, &roi, 0);
		} else {
			/*
			 * when sync_wait_broadcast enabled,
			 * need trigger at right ctrl to
			 * start both dcs cmd transmission
			 */
			other = mdss_dsi_get_other_ctrl(ctrl);
			if (!other)
				goto end;

			if (mdss_dsi_is_left_ctrl(ctrl)) {
				if (pinfo->partial_update_roi_merge) {
					/*
					 * roi is the one after merged
					 * to dsi-1 only
					 */
					mdss_dsi_send_col_page_addr(other,
							&roi, 0);
				} else {
					mdss_dsi_send_col_page_addr(ctrl,
							&ctrl->roi, 1);
					mdss_dsi_send_col_page_addr(other,
							&other->roi, 1);
				}
			} else {
				if (pinfo->partial_update_roi_merge) {
					/*
					 * roi is the one after merged
					 * to dsi-1 only
					 */
					mdss_dsi_send_col_page_addr(ctrl,
							&roi, 0);
				} else {
					mdss_dsi_send_col_page_addr(other,
							&other->roi, 1);
					mdss_dsi_send_col_page_addr(ctrl,
							&ctrl->roi, 1);
				}
			}
		}
	}

end:
	return 0;
}

void mdss_dsi_panel_3v_power(struct mdss_panel_data *pdata, int enable)
{
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	printk("LCD 3v_power GPIO(vsp):%d , Enable:%d\n",ctrl_pdata->lcd_3v_vsp_en_gpio, enable);

	if (enable) {
		if (gpio_is_valid(ctrl_pdata->lcd_3v_vsp_en_gpio)){
			//gpio_set_value((ctrl_pdata->lcd_5v_vsp_en_gpio), 1);	
			gpio_direction_output((ctrl_pdata->lcd_3v_vsp_en_gpio), 1);	
		}
		else
		{
			pr_debug("%s:%d, lcd_3v_vsp_en_gpio not configured\n",
			  	 __func__, __LINE__);
		}
#ifndef ZTE_SAMSUNG_ACL_HBM
		msleep(5);
#endif

	} else {
			if (gpio_is_valid(ctrl_pdata->lcd_3v_vsp_en_gpio)){
				
				//gpio_set_value((ctrl_pdata->lcd_5v_vsp_en_gpio), 0);
				gpio_direction_output((ctrl_pdata->lcd_3v_vsp_en_gpio), 0);
			}
#ifndef ZTE_SAMSUNG_ACL_HBM
			msleep(2);
#endif
	}
}

static void mdss_dsi_panel_switch_mode(struct mdss_panel_data *pdata,
							int mode)
{
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	struct mipi_panel_info *mipi;
	struct dsi_panel_cmds *pcmds;
	u32 flags = 0;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return;
	}

	mipi  = &pdata->panel_info.mipi;

	if (!mipi->dms_mode)
		return;

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	if (mipi->dms_mode != DYNAMIC_MODE_RESOLUTION_SWITCH_IMMEDIATE) {
		flags |= CMD_REQ_COMMIT;
		if (mode == SWITCH_TO_CMD_MODE)
			pcmds = &ctrl_pdata->video2cmd;
		else
			pcmds = &ctrl_pdata->cmd2video;
	} else if ((mipi->dms_mode ==
				DYNAMIC_MODE_RESOLUTION_SWITCH_IMMEDIATE)
			&& pdata->current_timing
			&& !list_empty(&pdata->timings_list)) {
		struct dsi_panel_timing *pt;

		pt = container_of(pdata->current_timing,
				struct dsi_panel_timing, timing);

		pr_debug("%s: sending switch commands\n", __func__);
		pcmds = &pt->switch_cmds;
		flags |= CMD_REQ_DMA_TPG;
		flags |= CMD_REQ_COMMIT;
	} else {
		pr_warn("%s: Invalid mode switch attempted\n", __func__);
		return;
	}

	if ((pdata->panel_info.compression_mode == COMPRESSION_DSC) &&
			(pdata->panel_info.send_pps_before_switch))
		mdss_dsi_panel_dsc_pps_send(ctrl_pdata, &pdata->panel_info);

	mdss_dsi_panel_cmds_send(ctrl_pdata, pcmds, flags);

	if ((pdata->panel_info.compression_mode == COMPRESSION_DSC) &&
			(!pdata->panel_info.send_pps_before_switch))
		mdss_dsi_panel_dsc_pps_send(ctrl_pdata, &pdata->panel_info);
}

static void mdss_dsi_panel_bl_ctrl(struct mdss_panel_data *pdata,
							u32 bl_level)
{
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	struct mdss_dsi_ctrl_pdata *sctrl = NULL;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	/*
	 * Some backlight controllers specify a minimum duty cycle
	 * for the backlight brightness. If the brightness is less
	 * than it, the controller can malfunction.
	 */

	if ((bl_level < pdata->panel_info.bl_min) && (bl_level != 0))
		bl_level = pdata->panel_info.bl_min;

	switch (ctrl_pdata->bklt_ctrl) {
	case BL_WLED:
		led_trigger_event(bl_led_trigger, bl_level);
		break;
	case BL_PWM:
		mdss_dsi_panel_bklt_pwm(ctrl_pdata, bl_level);
		break;
	case BL_DCS_CMD:
#ifdef ZTE_SAMSUNG_ACL_HBM
		if (!mdss_dsi_sync_wait_enable(ctrl_pdata)) {
			mdss_dsi_panel_bklt_dcs(ctrl_pdata, bl_level);
			if (bl_level == 255)
				mdss_dsi_panel_bklt_acl(ctrl_pdata, 0);
			else if (old_bl_level == 255 || old_bl_level == 0)
				mdss_dsi_panel_bklt_acl(ctrl_pdata, 2);
			break;
		}
		/*
		 * DCS commands to update backlight are usually sent at
		 * the same time to both the controllers. However, if
		 * sync_wait is enabled, we need to ensure that the
		 * dcs commands are first sent to the non-trigger
		 * controller so that when the commands are triggered,
		 * both controllers receive it at the same time.
		 */
		sctrl = mdss_dsi_get_other_ctrl(ctrl_pdata);
		if (mdss_dsi_sync_wait_trigger(ctrl_pdata)) {
			if (sctrl) {
				mdss_dsi_panel_bklt_dcs(sctrl, bl_level);
				if (bl_level == 255)
					mdss_dsi_panel_bklt_acl(sctrl, 0);
				else if (old_bl_level == 255 || old_bl_level == 0)
					mdss_dsi_panel_bklt_acl(sctrl, 2);
			}
			mdss_dsi_panel_bklt_dcs(ctrl_pdata, bl_level);
			if (bl_level == 255)
				mdss_dsi_panel_bklt_acl(ctrl_pdata, 0);
			else if (old_bl_level == 255 || old_bl_level == 0)
				mdss_dsi_panel_bklt_acl(ctrl_pdata, 2);
		} else {
			mdss_dsi_panel_bklt_dcs(ctrl_pdata, bl_level);
			if (bl_level == 255)
				mdss_dsi_panel_bklt_acl(ctrl_pdata, 0);
			else if (old_bl_level == 255 || old_bl_level == 0)
				mdss_dsi_panel_bklt_acl(ctrl_pdata, 2);
			if (sctrl) {
				mdss_dsi_panel_bklt_dcs(sctrl, bl_level);
				if (bl_level == 255)
					mdss_dsi_panel_bklt_acl(sctrl, 0);
				else if (old_bl_level == 255 || old_bl_level == 0)
					mdss_dsi_panel_bklt_acl(sctrl, 2);
			}
		}

		old_bl_level = bl_level;
		pr_info("%s: level=%d old_bl_level %d acl %d\n", __func__, bl_level, old_bl_level, led_acl_mode[1]);

#else
		if (!mdss_dsi_sync_wait_enable(ctrl_pdata)) {
			mdss_dsi_panel_bklt_dcs(ctrl_pdata, bl_level);
			break;
		}
		/*
		 * DCS commands to update backlight are usually sent at
		 * the same time to both the controllers. However, if
		 * sync_wait is enabled, we need to ensure that the
		 * dcs commands are first sent to the non-trigger
		 * controller so that when the commands are triggered,
		 * both controllers receive it at the same time.
		 */
		sctrl = mdss_dsi_get_other_ctrl(ctrl_pdata);
		if (mdss_dsi_sync_wait_trigger(ctrl_pdata)) {
			if (sctrl)
				mdss_dsi_panel_bklt_dcs(sctrl, bl_level);
			mdss_dsi_panel_bklt_dcs(ctrl_pdata, bl_level);
		} else {
			mdss_dsi_panel_bklt_dcs(ctrl_pdata, bl_level);
			if (sctrl)
				mdss_dsi_panel_bklt_dcs(sctrl, bl_level);
		}
#endif
		break;
	default:
		pr_err("%s: Unknown bl_ctrl configuration\n",
			__func__);
		break;
	}
}

static int mdss_dsi_panel_on(struct mdss_panel_data *pdata)
{
	struct mdss_dsi_ctrl_pdata *ctrl = NULL;
	struct mdss_panel_info *pinfo;
	struct dsi_panel_cmds *on_cmds;
	int ret = 0;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	display_on = true;

	pinfo = &pdata->panel_info;
	ctrl = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	pr_debug("%s: ndx=%d\n", __func__, ctrl->ndx);

	if (pinfo->dcs_cmd_by_left) {
		if (ctrl->ndx != DSI_CTRL_LEFT)
			goto end;
	}
	power_on_flag=1;
	on_cmds = &ctrl->on_cmds;

	if ((pinfo->mipi.dms_mode == DYNAMIC_MODE_SWITCH_IMMEDIATE) &&
			(pinfo->mipi.boot_mode != pinfo->mipi.mode))
		on_cmds = &ctrl->post_dms_on_cmds;

	pr_debug("%s: ndx=%d cmd_cnt=%d\n", __func__,
				ctrl->ndx, on_cmds->cmd_cnt);

	if (on_cmds->cmd_cnt)
		mdss_dsi_panel_cmds_send(ctrl, on_cmds, CMD_REQ_COMMIT);

	if (pinfo->compression_mode == COMPRESSION_DSC)
		mdss_dsi_panel_dsc_pps_send(ctrl, pinfo);

	if (ctrl->ds_registered)
		mdss_dba_utils_video_on(pinfo->dba_data, pinfo);
end:
	printk("LCD %s:-\n", __func__);
	return ret;
}

static int mdss_dsi_post_panel_on(struct mdss_panel_data *pdata)
{
	struct mdss_dsi_ctrl_pdata *ctrl = NULL;
	struct mdss_panel_info *pinfo;
	struct dsi_panel_cmds *cmds;
	u32 vsync_period = 0;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	pr_debug("%s: ctrl=%pK ndx=%d\n", __func__, ctrl, ctrl->ndx);

	pinfo = &pdata->panel_info;
	if (pinfo->dcs_cmd_by_left && ctrl->ndx != DSI_CTRL_LEFT)
			goto end;

	cmds = &ctrl->post_panel_on_cmds;
	if (cmds->cmd_cnt) {
		msleep(VSYNC_DELAY);	/* wait for a vsync passed */
		mdss_dsi_panel_cmds_send(ctrl, cmds, CMD_REQ_COMMIT);
	}

	if (pinfo->is_dba_panel && pinfo->is_pluggable) {
		/* ensure at least 1 frame transfers to down stream device */
		vsync_period = (MSEC_PER_SEC / pinfo->mipi.frame_rate) + 1;
		msleep(vsync_period);
		mdss_dba_utils_hdcp_enable(pinfo->dba_data, true);
	}

end:
	pr_debug("%s:-\n", __func__);
	return 0;
}

static int mdss_dsi_panel_off(struct mdss_panel_data *pdata)
{
	struct mdss_dsi_ctrl_pdata *ctrl = NULL;
	struct mdss_panel_info *pinfo;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	pinfo = &pdata->panel_info;
	ctrl = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	pr_debug("%s: ctrl=%pK ndx=%d\n", __func__, ctrl, ctrl->ndx);

	if (pinfo->dcs_cmd_by_left) {
		if (ctrl->ndx != DSI_CTRL_LEFT)
			goto end;
	}

	if (ctrl->off_cmds.cmd_cnt)
		mdss_dsi_panel_cmds_send(ctrl, &ctrl->off_cmds, CMD_REQ_COMMIT);

	if (ctrl->ds_registered && pinfo->is_pluggable) {
		mdss_dba_utils_video_off(pinfo->dba_data);
		mdss_dba_utils_hdcp_enable(pinfo->dba_data, false);
	}

	display_on = false;

end:
	printk("%s:-\n", __func__);
	return 0;
}

static int mdss_dsi_panel_low_power_config(struct mdss_panel_data *pdata,
	int enable)
{
	struct mdss_dsi_ctrl_pdata *ctrl = NULL;
	struct mdss_panel_info *pinfo;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	pinfo = &pdata->panel_info;
	ctrl = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	pr_debug("%s: ctrl=%pK ndx=%d enable=%d\n", __func__, ctrl, ctrl->ndx,
		enable);

	/* Any panel specific low power commands/config */

	pr_debug("%s:-\n", __func__);
	return 0;
}

static void mdss_dsi_parse_trigger(struct device_node *np, char *trigger,
		char *trigger_key)
{
	const char *data;

	*trigger = DSI_CMD_TRIGGER_SW;
	data = of_get_property(np, trigger_key, NULL);
	if (data) {
		if (!strcmp(data, "none"))
			*trigger = DSI_CMD_TRIGGER_NONE;
		else if (!strcmp(data, "trigger_te"))
			*trigger = DSI_CMD_TRIGGER_TE;
		else if (!strcmp(data, "trigger_sw_seof"))
			*trigger = DSI_CMD_TRIGGER_SW_SEOF;
		else if (!strcmp(data, "trigger_sw_te"))
			*trigger = DSI_CMD_TRIGGER_SW_TE;
	}
}


static int mdss_dsi_parse_dcs_cmds(struct device_node *np,
		struct dsi_panel_cmds *pcmds, char *cmd_key, char *link_key)
{
	const char *data;
	int blen = 0, len;
	char *buf, *bp;
	struct dsi_ctrl_hdr *dchdr;
	int i, cnt;

	data = of_get_property(np, cmd_key, &blen);
	if (!data) {
		pr_err("%s: failed, key=%s\n", __func__, cmd_key);
		return -ENOMEM;
	}

	buf = kzalloc(sizeof(char) * blen, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	memcpy(buf, data, blen);

	/* scan dcs commands */
	bp = buf;
	len = blen;
	cnt = 0;
	while (len >= sizeof(*dchdr)) {
		dchdr = (struct dsi_ctrl_hdr *)bp;
		dchdr->dlen = ntohs(dchdr->dlen);
		if (dchdr->dlen > len) {
			pr_err("%s: dtsi cmd=%x error, len=%d",
				__func__, dchdr->dtype, dchdr->dlen);
			goto exit_free;
		}
		bp += sizeof(*dchdr);
		len -= sizeof(*dchdr);
		bp += dchdr->dlen;
		len -= dchdr->dlen;
		cnt++;
	}

	if (len != 0) {
		pr_err("%s: dcs_cmd=%x len=%d error!",
				__func__, buf[0], blen);
		goto exit_free;
	}

	pcmds->cmds = kzalloc(cnt * sizeof(struct dsi_cmd_desc),
						GFP_KERNEL);
	if (!pcmds->cmds)
		goto exit_free;

	pcmds->cmd_cnt = cnt;
	pcmds->buf = buf;
	pcmds->blen = blen;

	bp = buf;
	len = blen;
	for (i = 0; i < cnt; i++) {
		dchdr = (struct dsi_ctrl_hdr *)bp;
		len -= sizeof(*dchdr);
		bp += sizeof(*dchdr);
		pcmds->cmds[i].dchdr = *dchdr;
		pcmds->cmds[i].payload = bp;
		bp += dchdr->dlen;
		len -= dchdr->dlen;
	}

	/*Set default link state to LP Mode*/
	pcmds->link_state = DSI_LP_MODE;

	if (link_key) {
		data = of_get_property(np, link_key, NULL);
		if (data && !strcmp(data, "dsi_hs_mode"))
			pcmds->link_state = DSI_HS_MODE;
		else
			pcmds->link_state = DSI_LP_MODE;
	}

	pr_debug("%s: dcs_cmd=%x len=%d, cmd_cnt=%d link_state=%d\n", __func__,
		pcmds->buf[0], pcmds->blen, pcmds->cmd_cnt, pcmds->link_state);

	return 0;

exit_free:
	kfree(buf);
	return -ENOMEM;
}


int mdss_panel_get_dst_fmt(u32 bpp, char mipi_mode, u32 pixel_packing,
				char *dst_format)
{
	int rc = 0;
	switch (bpp) {
	case 3:
		*dst_format = DSI_CMD_DST_FORMAT_RGB111;
		break;
	case 8:
		*dst_format = DSI_CMD_DST_FORMAT_RGB332;
		break;
	case 12:
		*dst_format = DSI_CMD_DST_FORMAT_RGB444;
		break;
	case 16:
		switch (mipi_mode) {
		case DSI_VIDEO_MODE:
			*dst_format = DSI_VIDEO_DST_FORMAT_RGB565;
			break;
		case DSI_CMD_MODE:
			*dst_format = DSI_CMD_DST_FORMAT_RGB565;
			break;
		default:
			*dst_format = DSI_VIDEO_DST_FORMAT_RGB565;
			break;
		}
		break;
	case 18:
		switch (mipi_mode) {
		case DSI_VIDEO_MODE:
			if (pixel_packing == 0)
				*dst_format = DSI_VIDEO_DST_FORMAT_RGB666;
			else
				*dst_format = DSI_VIDEO_DST_FORMAT_RGB666_LOOSE;
			break;
		case DSI_CMD_MODE:
			*dst_format = DSI_CMD_DST_FORMAT_RGB666;
			break;
		default:
			if (pixel_packing == 0)
				*dst_format = DSI_VIDEO_DST_FORMAT_RGB666;
			else
				*dst_format = DSI_VIDEO_DST_FORMAT_RGB666_LOOSE;
			break;
		}
		break;
	case 24:
		switch (mipi_mode) {
		case DSI_VIDEO_MODE:
			*dst_format = DSI_VIDEO_DST_FORMAT_RGB888;
			break;
		case DSI_CMD_MODE:
			*dst_format = DSI_CMD_DST_FORMAT_RGB888;
			break;
		default:
			*dst_format = DSI_VIDEO_DST_FORMAT_RGB888;
			break;
		}
		break;
	default:
		rc = -EINVAL;
		break;
	}
	return rc;
}

static int mdss_dsi_parse_fbc_params(struct device_node *np,
			struct mdss_panel_timing *timing)
{
	int rc, fbc_enabled = 0;
	u32 tmp;
	struct fbc_panel_info *fbc = &timing->fbc;

	fbc_enabled = of_property_read_bool(np,	"qcom,mdss-dsi-fbc-enable");
	if (fbc_enabled) {
		pr_debug("%s:%d FBC panel enabled.\n", __func__, __LINE__);
		fbc->enabled = 1;
		rc = of_property_read_u32(np, "qcom,mdss-dsi-fbc-bpp", &tmp);
		fbc->target_bpp = (!rc ? tmp : 24);
		rc = of_property_read_u32(np, "qcom,mdss-dsi-fbc-packing",
				&tmp);
		fbc->comp_mode = (!rc ? tmp : 0);
		fbc->qerr_enable = of_property_read_bool(np,
			"qcom,mdss-dsi-fbc-quant-error");
		rc = of_property_read_u32(np, "qcom,mdss-dsi-fbc-bias", &tmp);
		fbc->cd_bias = (!rc ? tmp : 0);
		fbc->pat_enable = of_property_read_bool(np,
				"qcom,mdss-dsi-fbc-pat-mode");
		fbc->vlc_enable = of_property_read_bool(np,
				"qcom,mdss-dsi-fbc-vlc-mode");
		fbc->bflc_enable = of_property_read_bool(np,
				"qcom,mdss-dsi-fbc-bflc-mode");
		rc = of_property_read_u32(np, "qcom,mdss-dsi-fbc-h-line-budget",
				&tmp);
		fbc->line_x_budget = (!rc ? tmp : 0);
		rc = of_property_read_u32(np, "qcom,mdss-dsi-fbc-budget-ctrl",
				&tmp);
		fbc->block_x_budget = (!rc ? tmp : 0);
		rc = of_property_read_u32(np, "qcom,mdss-dsi-fbc-block-budget",
				&tmp);
		fbc->block_budget = (!rc ? tmp : 0);
		rc = of_property_read_u32(np,
				"qcom,mdss-dsi-fbc-lossless-threshold", &tmp);
		fbc->lossless_mode_thd = (!rc ? tmp : 0);
		rc = of_property_read_u32(np,
				"qcom,mdss-dsi-fbc-lossy-threshold", &tmp);
		fbc->lossy_mode_thd = (!rc ? tmp : 0);
		rc = of_property_read_u32(np, "qcom,mdss-dsi-fbc-rgb-threshold",
				&tmp);
		fbc->lossy_rgb_thd = (!rc ? tmp : 0);
		rc = of_property_read_u32(np,
				"qcom,mdss-dsi-fbc-lossy-mode-idx", &tmp);
		fbc->lossy_mode_idx = (!rc ? tmp : 0);
		rc = of_property_read_u32(np,
				"qcom,mdss-dsi-fbc-slice-height", &tmp);
		fbc->slice_height = (!rc ? tmp : 0);
		fbc->pred_mode = of_property_read_bool(np,
				"qcom,mdss-dsi-fbc-2d-pred-mode");
		fbc->enc_mode = of_property_read_bool(np,
				"qcom,mdss-dsi-fbc-ver2-mode");
		rc = of_property_read_u32(np,
				"qcom,mdss-dsi-fbc-max-pred-err", &tmp);
		fbc->max_pred_err = (!rc ? tmp : 0);

		timing->compression_mode = COMPRESSION_FBC;
	} else {
		pr_debug("%s:%d Panel does not support FBC.\n",
				__func__, __LINE__);
		fbc->enabled = 0;
		fbc->target_bpp = 24;
	}
	return 0;
}

void mdss_dsi_panel_dsc_pps_send(struct mdss_dsi_ctrl_pdata *ctrl,
				struct mdss_panel_info *pinfo)
{
	struct dsi_panel_cmds pcmds;
	struct dsi_cmd_desc cmd;

	if (!pinfo || (pinfo->compression_mode != COMPRESSION_DSC))
		return;

	memset(&pcmds, 0, sizeof(pcmds));
	memset(&cmd, 0, sizeof(cmd));

	cmd.dchdr.dlen = mdss_panel_dsc_prepare_pps_buf(&pinfo->dsc,
		ctrl->pps_buf, 0);
	cmd.dchdr.dtype = DTYPE_PPS;
	cmd.dchdr.last = 1;
	cmd.dchdr.wait = 10;
	cmd.dchdr.vc = 0;
	cmd.dchdr.ack = 0;
	cmd.payload = ctrl->pps_buf;

	pcmds.cmd_cnt = 1;
	pcmds.cmds = &cmd;
	pcmds.link_state = DSI_LP_MODE;

	mdss_dsi_panel_cmds_send(ctrl, &pcmds, CMD_REQ_COMMIT);
}

static int mdss_dsi_parse_dsc_version(struct device_node *np,
		struct mdss_panel_timing *timing)
{
	u32 data;
	int rc = 0;
	struct dsc_desc *dsc = &timing->dsc;

	rc = of_property_read_u32(np, "qcom,mdss-dsc-version", &data);
	if (rc) {
		dsc->version = 0x11;
		rc = 0;
	} else {
		dsc->version = data & 0xff;
		/* only support DSC 1.1 rev */
		if (dsc->version != 0x11) {
			pr_err("%s: DSC version:%d not supported\n", __func__,
				dsc->version);
			rc = -EINVAL;
			goto end;
		}
	}

	rc = of_property_read_u32(np, "qcom,mdss-dsc-scr-version", &data);
	if (rc) {
		dsc->scr_rev = 0x0;
		rc = 0;
	} else {
		dsc->scr_rev = data & 0xff;
		/* only one scr rev supported */
		if (dsc->scr_rev > 0x1) {
			pr_err("%s: DSC scr version:%d not supported\n",
				__func__, dsc->scr_rev);
			rc = -EINVAL;
			goto end;
		}
	}

end:
	return rc;
}

static int mdss_dsi_parse_dsc_params(struct device_node *np,
		struct mdss_panel_timing *timing, bool is_split_display)
{
	u32 data, intf_width;
	int rc = 0;
	struct dsc_desc *dsc = &timing->dsc;

	if (!np) {
		pr_err("%s: device node pointer is NULL\n", __func__);
		return -EINVAL;
	}

	rc = of_property_read_u32(np, "qcom,mdss-dsc-encoders", &data);
	if (rc) {
		if (!of_find_property(np, "qcom,mdss-dsc-encoders", NULL)) {
			/* property is not defined, default to 1 */
			data = 1;
		} else {
			pr_err("%s: Error parsing qcom,mdss-dsc-encoders\n",
				__func__);
			goto end;
		}
	}

	timing->dsc_enc_total = data;

	if (is_split_display && (timing->dsc_enc_total > 1)) {
		pr_err("%s: Error: for split displays, more than 1 dsc encoder per panel is not allowed.\n",
			__func__);
		goto end;
	}

	rc = of_property_read_u32(np, "qcom,mdss-dsc-slice-height", &data);
	if (rc)
		goto end;
	dsc->slice_height = data;

	rc = of_property_read_u32(np, "qcom,mdss-dsc-slice-width", &data);
	if (rc)
		goto end;
	dsc->slice_width = data;
	intf_width = timing->xres;

	if (intf_width % dsc->slice_width) {
		pr_err("%s: Error: multiple of slice-width:%d should match panel-width:%d\n",
			__func__, dsc->slice_width, intf_width);
		goto end;
	}

	data = intf_width / dsc->slice_width;
	if (((timing->dsc_enc_total > 1) && ((data != 2) && (data != 4))) ||
	    ((timing->dsc_enc_total == 1) && (data > 2))) {
		pr_err("%s: Error: max 2 slice per encoder. slice-width:%d should match panel-width:%d dsc_enc_total:%d\n",
			__func__, dsc->slice_width,
			intf_width, timing->dsc_enc_total);
		goto end;
	}

	rc = of_property_read_u32(np, "qcom,mdss-dsc-slice-per-pkt", &data);
	if (rc)
		goto end;
	dsc->slice_per_pkt = data;

	/*
	 * slice_per_pkt can be either 1 or all slices_per_intf
	 */
	if ((dsc->slice_per_pkt > 1) && (dsc->slice_per_pkt !=
			DIV_ROUND_UP(intf_width, dsc->slice_width))) {
		pr_err("Error: slice_per_pkt can be either 1 or all slices_per_intf\n");
		pr_err("%s: slice_per_pkt=%d, slice_width=%d intf_width=%d\n",
			__func__,
			dsc->slice_per_pkt, dsc->slice_width, intf_width);
		rc = -EINVAL;
		goto end;
	}

	pr_debug("%s: num_enc:%d :slice h=%d w=%d s_pkt=%d\n", __func__,
		timing->dsc_enc_total, dsc->slice_height,
		dsc->slice_width, dsc->slice_per_pkt);

	rc = of_property_read_u32(np, "qcom,mdss-dsc-bit-per-component", &data);
	if (rc)
		goto end;
	dsc->bpc = data;

	rc = of_property_read_u32(np, "qcom,mdss-dsc-bit-per-pixel", &data);
	if (rc)
		goto end;
	dsc->bpp = data;

	pr_debug("%s: bpc=%d bpp=%d\n", __func__,
		dsc->bpc, dsc->bpp);

	dsc->block_pred_enable = of_property_read_bool(np,
			"qcom,mdss-dsc-block-prediction-enable");

	dsc->enable_422 = 0;
	dsc->convert_rgb = 1;
	dsc->vbr_enable = 0;

	dsc->config_by_manufacture_cmd = of_property_read_bool(np,
		"qcom,mdss-dsc-config-by-manufacture-cmd");

	mdss_panel_dsc_parameters_calc(&timing->dsc);
	mdss_panel_dsc_pclk_param_calc(&timing->dsc, intf_width);

	timing->dsc.full_frame_slices =
		DIV_ROUND_UP(intf_width, timing->dsc.slice_width);

	timing->compression_mode = COMPRESSION_DSC;

end:
	return rc;
}

static struct device_node *mdss_dsi_panel_get_dsc_cfg_np(
		struct device_node *np, struct mdss_panel_data *panel_data,
		bool default_timing)
{
	struct device_node *dsc_cfg_np = NULL;


	/* Read the dsc config node specified by command line */
	if (default_timing) {
		dsc_cfg_np = of_get_child_by_name(np,
				panel_data->dsc_cfg_np_name);
		if (!dsc_cfg_np)
			pr_warn_once("%s: cannot find dsc config node:%s\n",
				__func__, panel_data->dsc_cfg_np_name);
	}

	/*
	 * Fall back to default from DT as nothing is specified
	 * in command line.
	 */
	if (!dsc_cfg_np && of_find_property(np, "qcom,config-select", NULL)) {
		dsc_cfg_np = of_parse_phandle(np, "qcom,config-select", 0);
		if (!dsc_cfg_np)
			pr_warn_once("%s:err parsing qcom,config-select\n",
					__func__);
	}

	return dsc_cfg_np;
}

static int mdss_dsi_parse_topology_config(struct device_node *np,
	struct dsi_panel_timing *pt, struct mdss_panel_data *panel_data,
	bool default_timing)
{
	int rc = 0;
	bool is_split_display = panel_data->panel_info.is_split_display;
	const char *data;
	struct mdss_panel_timing *timing = &pt->timing;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	struct mdss_panel_info *pinfo;
	struct device_node *cfg_np = NULL;

	ctrl_pdata = container_of(panel_data, struct mdss_dsi_ctrl_pdata,
							panel_data);
	pinfo = &ctrl_pdata->panel_data.panel_info;

	cfg_np = mdss_dsi_panel_get_dsc_cfg_np(np,
				&ctrl_pdata->panel_data, default_timing);

	if (cfg_np) {
		if (!of_property_read_u32_array(cfg_np, "qcom,lm-split",
		    timing->lm_widths, 2)) {
			if (mdss_dsi_is_hw_config_split(ctrl_pdata->shared_data)
			    && (timing->lm_widths[1] != 0)) {
				pr_err("%s: lm-split not allowed with split display\n",
					__func__);
				rc = -EINVAL;
				goto end;
			}
		}
		rc = of_property_read_string(cfg_np, "qcom,split-mode", &data);
		if (!rc && !strcmp(data, "pingpong-split"))
			pinfo->use_pingpong_split = true;

		if (((timing->lm_widths[0]) || (timing->lm_widths[1])) &&
		    pinfo->use_pingpong_split) {
			pr_err("%s: pingpong_split cannot be used when lm-split[%d,%d] is specified\n",
				__func__,
				timing->lm_widths[0], timing->lm_widths[1]);
			return -EINVAL;
		}

		pr_info("%s: cfg_node name %s lm_split:%dx%d pp_split:%s\n",
			__func__, cfg_np->name,
			timing->lm_widths[0], timing->lm_widths[1],
			pinfo->use_pingpong_split ? "yes" : "no");
	}

	if (!pinfo->use_pingpong_split &&
	    (timing->lm_widths[0] == 0) && (timing->lm_widths[1] == 0))
		timing->lm_widths[0] = pt->timing.xres;

	data = of_get_property(np, "qcom,compression-mode", NULL);
	if (data) {
		if (cfg_np && !strcmp(data, "dsc")) {
			rc = mdss_dsi_parse_dsc_version(np, &pt->timing);
			if (rc)
				goto end;

			pinfo->send_pps_before_switch =
				of_property_read_bool(np,
				"qcom,mdss-dsi-send-pps-before-switch");

			rc = mdss_dsi_parse_dsc_params(cfg_np, &pt->timing,
					is_split_display);
		} else if (!strcmp(data, "fbc")) {
			rc = mdss_dsi_parse_fbc_params(np, &pt->timing);
		}
	}

end:
	of_node_put(cfg_np);
	return rc;
}

static void mdss_panel_parse_te_params(struct device_node *np,
		struct mdss_panel_timing *timing)
{
	struct mdss_mdp_pp_tear_check *te = &timing->te;
	u32 tmp;
	int rc = 0;
	/*
	 * TE default: dsi byte clock calculated base on 70 fps;
	 * around 14 ms to complete a kickoff cycle if te disabled;
	 * vclk_line base on 60 fps; write is faster than read;
	 * init == start == rdptr;
	 */
	te->tear_check_en =
		!of_property_read_bool(np, "qcom,mdss-tear-check-disable");
	rc = of_property_read_u32
		(np, "qcom,mdss-tear-check-sync-cfg-height", &tmp);
	te->sync_cfg_height = (!rc ? tmp : 0xfff0);
	rc = of_property_read_u32
		(np, "qcom,mdss-tear-check-sync-init-val", &tmp);
	te->vsync_init_val = (!rc ? tmp : timing->yres);
	rc = of_property_read_u32
		(np, "qcom,mdss-tear-check-sync-threshold-start", &tmp);
	te->sync_threshold_start = (!rc ? tmp : 4);
	rc = of_property_read_u32
		(np, "qcom,mdss-tear-check-sync-threshold-continue", &tmp);
	te->sync_threshold_continue = (!rc ? tmp : 4);
	rc = of_property_read_u32(np, "qcom,mdss-tear-check-frame-rate", &tmp);
	te->refx100 = (!rc ? tmp : 6000);
	rc = of_property_read_u32
		(np, "qcom,mdss-tear-check-start-pos", &tmp);
	te->start_pos = (!rc ? tmp : timing->yres);
	rc = of_property_read_u32
		(np, "qcom,mdss-tear-check-rd-ptr-trigger-intr", &tmp);
	te->rd_ptr_irq = (!rc ? tmp : timing->yres + 1);
	te->wr_ptr_irq = 0;
}


static int mdss_dsi_parse_reset_seq(struct device_node *np,
		u32 rst_seq[MDSS_DSI_RST_SEQ_LEN], u32 *rst_len,
		const char *name)
{
	int num = 0, i;
	int rc;
	struct property *data;
	u32 tmp[MDSS_DSI_RST_SEQ_LEN];
	*rst_len = 0;
	data = of_find_property(np, name, &num);
	num /= sizeof(u32);
	if (!data || !num || num > MDSS_DSI_RST_SEQ_LEN || num % 2) {
		pr_debug("%s:%d, error reading %s, length found = %d\n",
			__func__, __LINE__, name, num);
	} else {
		rc = of_property_read_u32_array(np, name, tmp, num);
		if (rc)
			pr_debug("%s:%d, error reading %s, rc = %d\n",
				__func__, __LINE__, name, rc);
		else {
			for (i = 0; i < num; ++i)
				rst_seq[i] = tmp[i];
			*rst_len = num;
		}
	}
	return 0;
}

static bool mdss_dsi_cmp_panel_reg_v2(struct mdss_dsi_ctrl_pdata *ctrl)
{
	int i, j;
	int len = 0, *lenp;
	int group = 0;

	lenp = ctrl->status_valid_params ?: ctrl->status_cmds_rlen;

	for (i = 0; i < ctrl->status_cmds.cmd_cnt; i++)
		len += lenp[i];

	for (j = 0; j < ctrl->groups; ++j) {
		for (i = 0; i < len; ++i) {
			if (ctrl->return_buf[i] !=
				ctrl->status_value[group + i])
				break;
		}

		if (i == len)
			return true;
		group += len;
	}

	return false;
}

static int mdss_dsi_gen_read_status(struct mdss_dsi_ctrl_pdata *ctrl_pdata)
{
	if (!mdss_dsi_cmp_panel_reg_v2(ctrl_pdata)) {
		pr_err("%s: Read back value from panel is incorrect\n",
							__func__);
		return -EINVAL;
	} else {
		return 1;
	}
}

static int mdss_dsi_nt35596_read_status(struct mdss_dsi_ctrl_pdata *ctrl_pdata)
{
	if (!mdss_dsi_cmp_panel_reg(ctrl_pdata->status_buf,
		ctrl_pdata->status_value, 0)) {
		ctrl_pdata->status_error_count = 0;
		pr_err("%s: Read back value from panel is incorrect\n",
							__func__);
		return -EINVAL;
	} else {
		if (!mdss_dsi_cmp_panel_reg(ctrl_pdata->status_buf,
			ctrl_pdata->status_value, 3)) {
			ctrl_pdata->status_error_count = 0;
		} else {
			if (mdss_dsi_cmp_panel_reg(ctrl_pdata->status_buf,
				ctrl_pdata->status_value, 4) ||
				mdss_dsi_cmp_panel_reg(ctrl_pdata->status_buf,
				ctrl_pdata->status_value, 5))
				ctrl_pdata->status_error_count = 0;
			else
				ctrl_pdata->status_error_count++;
			if (ctrl_pdata->status_error_count >=
					ctrl_pdata->max_status_error_count) {
				ctrl_pdata->status_error_count = 0;
				pr_err("%s: Read value bad. Error_cnt = %i\n",
					 __func__,
					ctrl_pdata->status_error_count);
				return -EINVAL;
			}
		}
		return 1;
	}
}

static void mdss_dsi_parse_roi_alignment(struct device_node *np,
		struct dsi_panel_timing *pt)
{
	int len = 0;
	u32 value[6];
	struct property *data;
	struct mdss_panel_timing *timing = &pt->timing;

	data = of_find_property(np, "qcom,panel-roi-alignment", &len);
	len /= sizeof(u32);
	if (!data || (len != 6)) {
		pr_debug("%s: Panel roi alignment not found", __func__);
	} else {
		int rc = of_property_read_u32_array(np,
				"qcom,panel-roi-alignment", value, len);
		if (rc)
			pr_debug("%s: Error reading panel roi alignment values",
					__func__);
		else {
			timing->roi_alignment.xstart_pix_align = value[0];
			timing->roi_alignment.ystart_pix_align = value[1];
			timing->roi_alignment.width_pix_align = value[2];
			timing->roi_alignment.height_pix_align = value[3];
			timing->roi_alignment.min_width = value[4];
			timing->roi_alignment.min_height = value[5];
		}

		pr_debug("%s: ROI alignment: [%d, %d, %d, %d, %d, %d]",
			__func__, timing->roi_alignment.xstart_pix_align,
			timing->roi_alignment.width_pix_align,
			timing->roi_alignment.ystart_pix_align,
			timing->roi_alignment.height_pix_align,
			timing->roi_alignment.min_width,
			timing->roi_alignment.min_height);
	}
}

static void mdss_dsi_parse_dms_config(struct device_node *np,
	struct mdss_dsi_ctrl_pdata *ctrl)
{
	struct mdss_panel_info *pinfo = &ctrl->panel_data.panel_info;
	const char *data;
	bool dms_enabled;

	dms_enabled = of_property_read_bool(np,
		"qcom,dynamic-mode-switch-enabled");

	if (!dms_enabled) {
		pinfo->mipi.dms_mode = DYNAMIC_MODE_SWITCH_DISABLED;
		goto exit;
	}

	/* default mode is suspend_resume */
	pinfo->mipi.dms_mode = DYNAMIC_MODE_SWITCH_SUSPEND_RESUME;
	data = of_get_property(np, "qcom,dynamic-mode-switch-type", NULL);
	if (data && !strcmp(data, "dynamic-resolution-switch-immediate")) {
		if (!list_empty(&ctrl->panel_data.timings_list))
			pinfo->mipi.dms_mode =
				DYNAMIC_MODE_RESOLUTION_SWITCH_IMMEDIATE;
		else
			pinfo->mipi.dms_mode =
				DYNAMIC_MODE_SWITCH_DISABLED;
		goto exit;
	}

	if (data && !strcmp(data, "dynamic-switch-immediate"))
		pinfo->mipi.dms_mode = DYNAMIC_MODE_SWITCH_IMMEDIATE;
	else
		pr_debug("%s: default dms suspend/resume\n", __func__);

	mdss_dsi_parse_dcs_cmds(np, &ctrl->video2cmd,
		"qcom,video-to-cmd-mode-switch-commands", NULL);

	mdss_dsi_parse_dcs_cmds(np, &ctrl->cmd2video,
		"qcom,cmd-to-video-mode-switch-commands", NULL);

	mdss_dsi_parse_dcs_cmds(np, &ctrl->post_dms_on_cmds,
		"qcom,mdss-dsi-post-mode-switch-on-command",
		"qcom,mdss-dsi-post-mode-switch-on-command-state");

	if (pinfo->mipi.dms_mode == DYNAMIC_MODE_SWITCH_IMMEDIATE &&
		!ctrl->post_dms_on_cmds.cmd_cnt) {
		pr_warn("%s: No post dms on cmd specified\n", __func__);
		pinfo->mipi.dms_mode = DYNAMIC_MODE_SWITCH_DISABLED;
	}

	if (!ctrl->video2cmd.cmd_cnt || !ctrl->cmd2video.cmd_cnt) {
		pr_warn("%s: No commands specified for dynamic switch\n",
			__func__);
		pinfo->mipi.dms_mode = DYNAMIC_MODE_SWITCH_DISABLED;
	}
exit:
	pr_info("%s: dynamic switch feature enabled: %d\n", __func__,
		pinfo->mipi.dms_mode);
	return;
}

/* the length of all the valid values to be checked should not be great
 * than the length of returned data from read command.
 */
static bool
mdss_dsi_parse_esd_check_valid_params(struct mdss_dsi_ctrl_pdata *ctrl)
{
	int i;

	for (i = 0; i < ctrl->status_cmds.cmd_cnt; ++i) {
		if (ctrl->status_valid_params[i] > ctrl->status_cmds_rlen[i]) {
			pr_debug("%s: ignore valid params!\n", __func__);
			return false;
		}
	}

	return true;
}

static bool mdss_dsi_parse_esd_status_len(struct device_node *np,
	char *prop_key, u32 **target, u32 cmd_cnt)
{
	int tmp;

	if (!of_find_property(np, prop_key, &tmp))
		return false;

	tmp /= sizeof(u32);
	if (tmp != cmd_cnt) {
		pr_err("%s: request property number(%d) not match command count(%d)\n",
			__func__, tmp, cmd_cnt);
		return false;
	}

	*target = kcalloc(tmp, sizeof(u32), GFP_KERNEL);
	if (IS_ERR_OR_NULL(*target)) {
		pr_err("%s: Error allocating memory for property\n",
			__func__);
		return false;
	}

	if (of_property_read_u32_array(np, prop_key, *target, tmp)) {
		pr_err("%s: cannot get values from dts\n", __func__);
		kfree(*target);
		*target = NULL;
		return false;
	}

	return true;
}

static void mdss_dsi_parse_esd_params(struct device_node *np,
	struct mdss_dsi_ctrl_pdata *ctrl)
{
	u32 tmp;
	u32 i, status_len, *lenp;
	int rc;
	struct property *data;
	const char *string;
	struct mdss_panel_info *pinfo = &ctrl->panel_data.panel_info;

	pinfo->esd_check_enabled = of_property_read_bool(np,
		"qcom,esd-check-enabled");

	if (!pinfo->esd_check_enabled)
		return;
/*zte,esd interrupt mode 0205  start */
#ifdef ZTE_FASTMMI_MANUFACTURING_VERSION
	ctrl->lcd_esd_interrupt_gpio = -2;
	pinfo->esd_check_enabled = 0;
	return;
#endif

	if (mdss_dsi_is_left_ctrl(ctrl)) {
		ctrl->lcd_esd_interrupt_gpio = of_get_named_gpio(np, "zte,esd-interrupt-gpio", 0);
		if (gpio_is_valid(ctrl->lcd_esd_interrupt_gpio))
			pr_err("LCD %s:, zte,esd-interrupt-gpio found:%d!\n", __func__, ctrl->lcd_esd_interrupt_gpio);
		else
			pr_err("LCD %s:, zte,esd-interrupt-gpio not specified :%d\n",
					__func__, ctrl->lcd_esd_interrupt_gpio);
	}
	/*zte,esd interrupt mode 0205  end */

	ctrl->status_mode = ESD_MAX;
	rc = of_property_read_string(np,
			"qcom,mdss-dsi-panel-status-check-mode", &string);
	if (!rc) {
		if (!strcmp(string, "bta_check")) {
			ctrl->status_mode = ESD_BTA;
		} else if (!strcmp(string, "reg_read")) {
			ctrl->status_mode = ESD_REG;
			ctrl->check_read_status =
				mdss_dsi_gen_read_status;
		} else if (!strcmp(string, "reg_read_nt35596")) {
			ctrl->status_mode = ESD_REG_NT35596;
			ctrl->status_error_count = 0;
			ctrl->check_read_status =
				mdss_dsi_nt35596_read_status;
		} else if (!strcmp(string, "te_signal_check")) {
			if (pinfo->mipi.mode == DSI_CMD_MODE) {
				ctrl->status_mode = ESD_TE;
			} else {
				pr_err("TE-ESD not valid for video mode\n");
				goto error;
			}
		} else {
			pr_err("No valid panel-status-check-mode string\n");
			goto error;
		}
	}

	if ((ctrl->status_mode == ESD_BTA) || (ctrl->status_mode == ESD_TE) ||
			(ctrl->status_mode == ESD_MAX))
		return;

	mdss_dsi_parse_dcs_cmds(np, &ctrl->status_cmds,
			"qcom,mdss-dsi-panel-status-command",
				"qcom,mdss-dsi-panel-status-command-state");

	rc = of_property_read_u32(np, "qcom,mdss-dsi-panel-max-error-count",
		&tmp);
	ctrl->max_status_error_count = (!rc ? tmp : 0);

	if (!mdss_dsi_parse_esd_status_len(np,
		"qcom,mdss-dsi-panel-status-read-length",
		&ctrl->status_cmds_rlen, ctrl->status_cmds.cmd_cnt)) {
		pinfo->esd_check_enabled = false;
		return;
	}

	if (mdss_dsi_parse_esd_status_len(np,
		"qcom,mdss-dsi-panel-status-valid-params",
		&ctrl->status_valid_params, ctrl->status_cmds.cmd_cnt)) {
		if (!mdss_dsi_parse_esd_check_valid_params(ctrl))
			goto error1;
	}

	status_len = 0;
	lenp = ctrl->status_valid_params ?: ctrl->status_cmds_rlen;
	for (i = 0; i < ctrl->status_cmds.cmd_cnt; ++i)
		status_len += lenp[i];

	data = of_find_property(np, "qcom,mdss-dsi-panel-status-value", &tmp);
	tmp /= sizeof(u32);
	if (!IS_ERR_OR_NULL(data) && tmp != 0 && (tmp % status_len) == 0) {
		ctrl->groups = tmp / status_len;
	} else {
		pr_err("%s: Error parse panel-status-value\n", __func__);
		goto error1;
	}

	ctrl->status_value = kzalloc(sizeof(u32) * status_len * ctrl->groups,
				GFP_KERNEL);
	if (!ctrl->status_value)
		goto error1;

	ctrl->return_buf = kcalloc(status_len * ctrl->groups,
			sizeof(unsigned char), GFP_KERNEL);
	if (!ctrl->return_buf)
		goto error2;

	rc = of_property_read_u32_array(np,
		"qcom,mdss-dsi-panel-status-value",
		ctrl->status_value, ctrl->groups * status_len);
	if (rc) {
		pr_debug("%s: Error reading panel status values\n",
				__func__);
		memset(ctrl->status_value, 0, ctrl->groups * status_len);
	}

	return;

error2:
	kfree(ctrl->status_value);
error1:
	kfree(ctrl->status_valid_params);
	kfree(ctrl->status_cmds_rlen);
error:
	pinfo->esd_check_enabled = false;
}

static int mdss_dsi_parse_panel_features(struct device_node *np,
	struct mdss_dsi_ctrl_pdata *ctrl)
{
	struct mdss_panel_info *pinfo;

	if (!np || !ctrl) {
		pr_err("%s: Invalid arguments\n", __func__);
		return -ENODEV;
	}

	pinfo = &ctrl->panel_data.panel_info;

	pinfo->partial_update_supported = of_property_read_bool(np,
		"qcom,partial-update-enabled");
	if (pinfo->mipi.mode == DSI_CMD_MODE) {
		pinfo->partial_update_enabled = pinfo->partial_update_supported;
		pr_info("%s: partial_update_enabled=%d\n", __func__,
					pinfo->partial_update_enabled);
		ctrl->set_col_page_addr = mdss_dsi_set_col_page_addr;
		if (pinfo->partial_update_enabled) {
			pinfo->partial_update_roi_merge =
					of_property_read_bool(np,
					"qcom,partial-update-roi-merge");
		}
	}

	pinfo->dcs_cmd_by_left = of_property_read_bool(np,
		"qcom,dcs-cmd-by-left");

	pinfo->ulps_feature_enabled = of_property_read_bool(np,
		"qcom,ulps-enabled");
	pr_info("%s: ulps feature %s\n", __func__,
		(pinfo->ulps_feature_enabled ? "enabled" : "disabled"));

	pinfo->ulps_suspend_enabled = of_property_read_bool(np,
		"qcom,suspend-ulps-enabled");
	pr_info("%s: ulps during suspend feature %s", __func__,
		(pinfo->ulps_suspend_enabled ? "enabled" : "disabled"));

	mdss_dsi_parse_dms_config(np, ctrl);

	pinfo->panel_ack_disabled = pinfo->sim_panel_mode ?
		1 : of_property_read_bool(np, "qcom,panel-ack-disabled");

	pinfo->allow_phy_power_off = of_property_read_bool(np,
		"qcom,panel-allow-phy-poweroff");

	mdss_dsi_parse_esd_params(np, ctrl);

	if (pinfo->panel_ack_disabled && pinfo->esd_check_enabled) {
		pr_warn("ESD should not be enabled if panel ACK is disabled\n");
		pinfo->esd_check_enabled = false;
	}

	if (ctrl->disp_en_gpio <= 0) {
		ctrl->disp_en_gpio = of_get_named_gpio(
			np,
			"qcom,5v-boost-gpio", 0);

		if (!gpio_is_valid(ctrl->disp_en_gpio))
			pr_debug("%s:%d, Disp_en gpio not specified\n",
					__func__, __LINE__);
	}

	return 0;
}

static void mdss_dsi_parse_panel_horizintal_line_idle(struct device_node *np,
	struct mdss_dsi_ctrl_pdata *ctrl)
{
	const u32 *src;
	int i, len, cnt;
	struct panel_horizontal_idle *kp;

	if (!np || !ctrl) {
		pr_err("%s: Invalid arguments\n", __func__);
		return;
	}

	src = of_get_property(np, "qcom,mdss-dsi-hor-line-idle", &len);
	if (!src || len == 0)
		return;

	cnt = len % 3; /* 3 fields per entry */
	if (cnt) {
		pr_err("%s: invalid horizontal idle len=%d\n", __func__, len);
		return;
	}

	cnt = len / sizeof(u32);

	kp = kzalloc(sizeof(*kp) * (cnt / 3), GFP_KERNEL);
	if (kp == NULL) {
		pr_err("%s: No memory\n", __func__);
		return;
	}

	ctrl->line_idle = kp;
	for (i = 0; i < cnt; i += 3) {
		kp->min = be32_to_cpu(src[i]);
		kp->max = be32_to_cpu(src[i+1]);
		kp->idle = be32_to_cpu(src[i+2]);
		kp++;
		ctrl->horizontal_idle_cnt++;
	}

	/*
	 * idle is enabled for this controller, this will be used to
	 * enable/disable burst mode since both features are mutually
	 * exclusive.
	 */
	ctrl->idle_enabled = true;

	pr_debug("%s: horizontal_idle_cnt=%d\n", __func__,
				ctrl->horizontal_idle_cnt);
}

static int mdss_dsi_set_refresh_rate_range(struct device_node *pan_node,
		struct mdss_panel_info *pinfo)
{
	int rc = 0;
	rc = of_property_read_u32(pan_node,
			"qcom,mdss-dsi-min-refresh-rate",
			&pinfo->min_fps);
	if (rc) {
		pr_warn("%s:%d, Unable to read min refresh rate\n",
				__func__, __LINE__);

		/*
		 * Since min refresh rate is not specified when dynamic
		 * fps is enabled, using minimum as 30
		 */
		pinfo->min_fps = MIN_REFRESH_RATE;
		rc = 0;
	}

	rc = of_property_read_u32(pan_node,
			"qcom,mdss-dsi-max-refresh-rate",
			&pinfo->max_fps);
	if (rc) {
		pr_warn("%s:%d, Unable to read max refresh rate\n",
				__func__, __LINE__);

		/*
		 * Since max refresh rate was not specified when dynamic
		 * fps is enabled, using the default panel refresh rate
		 * as max refresh rate supported.
		 */
		pinfo->max_fps = pinfo->mipi.frame_rate;
		rc = 0;
	}

	pr_info("dyn_fps: min = %d, max = %d\n",
			pinfo->min_fps, pinfo->max_fps);
	return rc;
}

static void mdss_dsi_parse_dfps_config(struct device_node *pan_node,
			struct mdss_dsi_ctrl_pdata *ctrl_pdata)
{
	const char *data;
	bool dynamic_fps;
	struct mdss_panel_info *pinfo = &(ctrl_pdata->panel_data.panel_info);

	dynamic_fps = of_property_read_bool(pan_node,
			"qcom,mdss-dsi-pan-enable-dynamic-fps");

	if (!dynamic_fps)
		return;

	pinfo->dynamic_fps = true;
	data = of_get_property(pan_node, "qcom,mdss-dsi-pan-fps-update", NULL);
	if (data) {
		if (!strcmp(data, "dfps_suspend_resume_mode")) {
			pinfo->dfps_update = DFPS_SUSPEND_RESUME_MODE;
			pr_debug("dfps mode: suspend/resume\n");
		} else if (!strcmp(data, "dfps_immediate_clk_mode")) {
			pinfo->dfps_update = DFPS_IMMEDIATE_CLK_UPDATE_MODE;
			pr_debug("dfps mode: Immediate clk\n");
		} else if (!strcmp(data, "dfps_immediate_porch_mode_hfp")) {
			pinfo->dfps_update =
				DFPS_IMMEDIATE_PORCH_UPDATE_MODE_HFP;
			pr_debug("dfps mode: Immediate porch HFP\n");
		} else if (!strcmp(data, "dfps_immediate_porch_mode_vfp")) {
			pinfo->dfps_update =
				DFPS_IMMEDIATE_PORCH_UPDATE_MODE_VFP;
			pr_debug("dfps mode: Immediate porch VFP\n");
		} else {
			pinfo->dfps_update = DFPS_SUSPEND_RESUME_MODE;
			pr_debug("default dfps mode: suspend/resume\n");
		}
		mdss_dsi_set_refresh_rate_range(pan_node, pinfo);
	} else {
		pinfo->dynamic_fps = false;
		pr_debug("dfps update mode not configured: disable\n");
	}
	pinfo->new_fps = pinfo->mipi.frame_rate;
	pinfo->current_fps = pinfo->mipi.frame_rate;

	return;
}

int mdss_panel_parse_bl_settings(struct device_node *np,
			struct mdss_dsi_ctrl_pdata *ctrl_pdata)
{
	const char *data;
	int rc = 0;
	u32 tmp;

	ctrl_pdata->bklt_ctrl = UNKNOWN_CTRL;
	data = of_get_property(np, "qcom,mdss-dsi-bl-pmic-control-type", NULL);
	if (data) {
		if (!strcmp(data, "bl_ctrl_wled")) {
			led_trigger_register_simple("bkl-trigger",
				&bl_led_trigger);
			pr_debug("%s: SUCCESS-> WLED TRIGGER register\n",
				__func__);
			ctrl_pdata->bklt_ctrl = BL_WLED;
		} else if (!strcmp(data, "bl_ctrl_pwm")) {
			ctrl_pdata->bklt_ctrl = BL_PWM;
			ctrl_pdata->pwm_pmi = of_property_read_bool(np,
					"qcom,mdss-dsi-bl-pwm-pmi");
			rc = of_property_read_u32(np,
				"qcom,mdss-dsi-bl-pmic-pwm-frequency", &tmp);
			if (rc) {
				pr_err("%s:%d, Error, panel pwm_period\n",
						__func__, __LINE__);
				return -EINVAL;
			}
			ctrl_pdata->pwm_period = tmp;
			if (ctrl_pdata->pwm_pmi) {
				ctrl_pdata->pwm_bl = of_pwm_get(np, NULL);
				if (IS_ERR(ctrl_pdata->pwm_bl)) {
					pr_err("%s: Error, pwm device\n",
								__func__);
					ctrl_pdata->pwm_bl = NULL;
					return -EINVAL;
				}
			} else {
				rc = of_property_read_u32(np,
					"qcom,mdss-dsi-bl-pmic-bank-select",
								 &tmp);
				if (rc) {
					pr_err("%s:%d, Error, lpg channel\n",
							__func__, __LINE__);
					return -EINVAL;
				}
				ctrl_pdata->pwm_lpg_chan = tmp;
				tmp = of_get_named_gpio(np,
					"qcom,mdss-dsi-pwm-gpio", 0);
				ctrl_pdata->pwm_pmic_gpio = tmp;
				pr_debug("%s: Configured PWM bklt ctrl\n",
								 __func__);
			}
		} else if (!strcmp(data, "bl_ctrl_dcs")) {
			ctrl_pdata->bklt_ctrl = BL_DCS_CMD;
			pr_debug("%s: Configured DCS_CMD bklt ctrl\n",
								__func__);
		}
	}
	return 0;
}

int mdss_dsi_panel_timing_switch(struct mdss_dsi_ctrl_pdata *ctrl,
			struct mdss_panel_timing *timing)
{
	struct dsi_panel_timing *pt;
	struct mdss_panel_info *pinfo = &ctrl->panel_data.panel_info;
	int i;

	if (!timing)
		return -EINVAL;

	if (timing == ctrl->panel_data.current_timing) {
		pr_warn("%s: panel timing \"%s\" already set\n", __func__,
				timing->name);
		return 0; /* nothing to do */
	}

	pr_debug("%s: ndx=%d switching to panel timing \"%s\"\n", __func__,
			ctrl->ndx, timing->name);

	mdss_panel_info_from_timing(timing, pinfo);

	pt = container_of(timing, struct dsi_panel_timing, timing);
	pinfo->mipi.t_clk_pre = pt->t_clk_pre;
	pinfo->mipi.t_clk_post = pt->t_clk_post;

	for (i = 0; i < ARRAY_SIZE(pt->phy_timing); i++)
		pinfo->mipi.dsi_phy_db.timing[i] = pt->phy_timing[i];

	for (i = 0; i < ARRAY_SIZE(pt->phy_timing_8996); i++)
		pinfo->mipi.dsi_phy_db.timing_8996[i] = pt->phy_timing_8996[i];

	ctrl->on_cmds = pt->on_cmds;
	ctrl->post_panel_on_cmds = pt->post_panel_on_cmds;

	ctrl->panel_data.current_timing = timing;
	if (!timing->clk_rate)
		ctrl->refresh_clk_rate = true;
	mdss_dsi_clk_refresh(&ctrl->panel_data, ctrl->update_phy_timing);

	return 0;
}

void mdss_dsi_unregister_bl_settings(struct mdss_dsi_ctrl_pdata *ctrl_pdata)
{
	if (ctrl_pdata->bklt_ctrl == BL_WLED)
		led_trigger_unregister_simple(bl_led_trigger);
}

static int mdss_dsi_panel_timing_from_dt(struct device_node *np,
		struct dsi_panel_timing *pt,
		struct mdss_panel_data *panel_data)
{
	u32 tmp;
	u64 tmp64;
	int rc, i, len;
	const char *data;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata;
	struct mdss_panel_info *pinfo;
	bool phy_timings_present = false;

	pinfo = &panel_data->panel_info;

	ctrl_pdata = container_of(panel_data, struct mdss_dsi_ctrl_pdata,
				panel_data);

	rc = of_property_read_u32(np, "qcom,mdss-dsi-panel-width", &tmp);
	if (rc) {
		pr_err("%s:%d, panel width not specified\n",
						__func__, __LINE__);
		return -EINVAL;
	}
	pt->timing.xres = tmp;

	rc = of_property_read_u32(np, "qcom,mdss-dsi-panel-height", &tmp);
	if (rc) {
		pr_err("%s:%d, panel height not specified\n",
						__func__, __LINE__);
		return -EINVAL;
	}
	pt->timing.yres = tmp;

	rc = of_property_read_u32(np, "qcom,mdss-dsi-h-front-porch", &tmp);
	pt->timing.h_front_porch = (!rc ? tmp : 6);
	rc = of_property_read_u32(np, "qcom,mdss-dsi-h-back-porch", &tmp);
	pt->timing.h_back_porch = (!rc ? tmp : 6);
	rc = of_property_read_u32(np, "qcom,mdss-dsi-h-pulse-width", &tmp);
	pt->timing.h_pulse_width = (!rc ? tmp : 2);
	rc = of_property_read_u32(np, "qcom,mdss-dsi-h-sync-skew", &tmp);
	pt->timing.hsync_skew = (!rc ? tmp : 0);
	rc = of_property_read_u32(np, "qcom,mdss-dsi-v-back-porch", &tmp);
	pt->timing.v_back_porch = (!rc ? tmp : 6);
	rc = of_property_read_u32(np, "qcom,mdss-dsi-v-front-porch", &tmp);
	pt->timing.v_front_porch = (!rc ? tmp : 6);
	rc = of_property_read_u32(np, "qcom,mdss-dsi-v-pulse-width", &tmp);
	pt->timing.v_pulse_width = (!rc ? tmp : 2);

	rc = of_property_read_u32(np, "qcom,mdss-dsi-h-left-border", &tmp);
	pt->timing.border_left = !rc ? tmp : 0;
	rc = of_property_read_u32(np, "qcom,mdss-dsi-h-right-border", &tmp);
	pt->timing.border_right = !rc ? tmp : 0;

	/* overriding left/right borders for split display cases */
	if (mdss_dsi_is_hw_config_split(ctrl_pdata->shared_data)) {
		if (panel_data->next)
			pt->timing.border_right = 0;
		else
			pt->timing.border_left = 0;
	}

	rc = of_property_read_u32(np, "qcom,mdss-dsi-v-top-border", &tmp);
	pt->timing.border_top = !rc ? tmp : 0;
	rc = of_property_read_u32(np, "qcom,mdss-dsi-v-bottom-border", &tmp);
	pt->timing.border_bottom = !rc ? tmp : 0;

	rc = of_property_read_u32(np, "qcom,mdss-dsi-panel-framerate", &tmp);
	pt->timing.frame_rate = !rc ? tmp : DEFAULT_FRAME_RATE;
	rc = of_property_read_u64(np, "qcom,mdss-dsi-panel-clockrate", &tmp64);
	if (rc == -EOVERFLOW) {
		tmp64 = 0;
		rc = of_property_read_u32(np,
			"qcom,mdss-dsi-panel-clockrate", (u32 *)&tmp64);
	}
	pt->timing.clk_rate = !rc ? tmp64 : 0;

	data = of_get_property(np, "qcom,mdss-dsi-panel-timings", &len);
	if ((!data) || (len != 12)) {
		pr_debug("%s:%d, Unable to read Phy timing settings",
		       __func__, __LINE__);
	} else {
		for (i = 0; i < len; i++)
			pt->phy_timing[i] = data[i];
		phy_timings_present = true;
	}

	data = of_get_property(np, "qcom,mdss-dsi-panel-timings-phy-v2", &len);
	if ((!data) || (len != 40)) {
		pr_debug("%s:%d, Unable to read 8996 Phy lane timing settings",
		       __func__, __LINE__);
	} else {
		for (i = 0; i < len; i++)
			pt->phy_timing_8996[i] = data[i];
		phy_timings_present = true;
	}
	if (!phy_timings_present) {
		pr_err("%s: phy timing settings not present\n", __func__);
		return -EINVAL;
	}

	rc = of_property_read_u32(np, "qcom,mdss-dsi-t-clk-pre", &tmp);
	pt->t_clk_pre = (!rc ? tmp : 0x24);
	rc = of_property_read_u32(np, "qcom,mdss-dsi-t-clk-post", &tmp);
	pt->t_clk_post = (!rc ? tmp : 0x03);

	if (np->name) {
		pt->timing.name = kstrdup(np->name, GFP_KERNEL);
		pr_info("%s: found new timing \"%s\" (%pK)\n", __func__,
				np->name, &pt->timing);
	}

	return 0;
}

static int  mdss_dsi_panel_config_res_properties(struct device_node *np,
		struct dsi_panel_timing *pt,
		struct mdss_panel_data *panel_data,
		bool default_timing)
{
	int rc = 0;

	mdss_dsi_parse_roi_alignment(np, pt);
#ifdef ZTE_SAMSUNG_ACL_HBM
	mdss_dsi_parse_dcs_cmds(np, &pt->on_cmds,
				"qcom,mdss-dsi-on-command-acl",
				"qcom,mdss-dsi-on-command-state");
#else
	mdss_dsi_parse_dcs_cmds(np, &pt->on_cmds,
				"qcom,mdss-dsi-on-command",
				"qcom,mdss-dsi-on-command-state");
#endif
	mdss_dsi_parse_dcs_cmds(np, &pt->post_panel_on_cmds,
		"qcom,mdss-dsi-post-panel-on-command", NULL);

	mdss_dsi_parse_dcs_cmds(np, &pt->switch_cmds,
		"qcom,mdss-dsi-timing-switch-command",
		"qcom,mdss-dsi-timing-switch-command-state");

	rc = mdss_dsi_parse_topology_config(np, pt, panel_data, default_timing);
	if (rc) {
		pr_err("%s: parsing compression params failed. rc:%d\n",
			__func__, rc);
		return rc;
	}

	mdss_panel_parse_te_params(np, &pt->timing);
	return rc;
}

static int mdss_panel_parse_display_timings(struct device_node *np,
		struct mdss_panel_data *panel_data)
{
	struct mdss_dsi_ctrl_pdata *ctrl;
	struct dsi_panel_timing *modedb;
	struct device_node *timings_np;
	struct device_node *entry;
	int num_timings, rc;
	int i = 0, active_ndx = 0;
	bool default_timing = false;

	ctrl = container_of(panel_data, struct mdss_dsi_ctrl_pdata, panel_data);

	INIT_LIST_HEAD(&panel_data->timings_list);

	timings_np = of_get_child_by_name(np, "qcom,mdss-dsi-display-timings");
	if (!timings_np) {
		struct dsi_panel_timing pt;
		memset(&pt, 0, sizeof(struct dsi_panel_timing));

		/*
		 * display timings node is not available, fallback to reading
		 * timings directly from root node instead
		 */
		pr_debug("reading display-timings from panel node\n");
		rc = mdss_dsi_panel_timing_from_dt(np, &pt, panel_data);
		if (!rc) {
			mdss_dsi_panel_config_res_properties(np, &pt,
					panel_data, true);
			rc = mdss_dsi_panel_timing_switch(ctrl, &pt.timing);
		}
		return rc;
	}

	num_timings = of_get_child_count(timings_np);
	if (num_timings == 0) {
		pr_err("no timings found within display-timings\n");
		rc = -EINVAL;
		goto exit;
	}

	modedb = kcalloc(num_timings, sizeof(*modedb), GFP_KERNEL);
	if (!modedb) {
		rc = -ENOMEM;
		goto exit;
	}

	for_each_child_of_node(timings_np, entry) {
		rc = mdss_dsi_panel_timing_from_dt(entry, (modedb + i),
				panel_data);
		if (rc) {
			kfree(modedb);
			goto exit;
		}

		default_timing = of_property_read_bool(entry,
				"qcom,mdss-dsi-timing-default");
		if (default_timing)
			active_ndx = i;

		mdss_dsi_panel_config_res_properties(entry, (modedb + i),
				panel_data, default_timing);

		list_add(&modedb[i].timing.list,
				&panel_data->timings_list);
		i++;
	}

	/* Configure default timing settings */
	rc = mdss_dsi_panel_timing_switch(ctrl, &modedb[active_ndx].timing);
	if (rc)
		pr_err("unable to configure default timing settings\n");

exit:
	of_node_put(timings_np);

	return rc;
}

static int mdss_panel_parse_dt(struct device_node *np,
			struct mdss_dsi_ctrl_pdata *ctrl_pdata)
{
	u32 tmp;
	int rc, len = 0;
	const char *data;
	static const char *pdest;
	const char *bridge_chip_name;
	struct mdss_panel_info *pinfo = &(ctrl_pdata->panel_data.panel_info);

	if (mdss_dsi_is_hw_config_split(ctrl_pdata->shared_data))
		pinfo->is_split_display = true;

	rc = of_property_read_u32(np,
		"qcom,mdss-pan-physical-width-dimension", &tmp);
	pinfo->physical_width = (!rc ? tmp : 0);
	rc = of_property_read_u32(np,
		"qcom,mdss-pan-physical-height-dimension", &tmp);
	pinfo->physical_height = (!rc ? tmp : 0);

	rc = of_property_read_u32(np, "qcom,mdss-dsi-bpp", &tmp);
	if (rc) {
		pr_err("%s:%d, bpp not specified\n", __func__, __LINE__);
		return -EINVAL;
	}
	pinfo->bpp = (!rc ? tmp : 24);
	pinfo->mipi.mode = DSI_VIDEO_MODE;
	data = of_get_property(np, "qcom,mdss-dsi-panel-type", NULL);
	if (data && !strncmp(data, "dsi_cmd_mode", 12))
		pinfo->mipi.mode = DSI_CMD_MODE;
	pinfo->mipi.boot_mode = pinfo->mipi.mode;
	tmp = 0;
	data = of_get_property(np, "qcom,mdss-dsi-pixel-packing", NULL);
	if (data && !strcmp(data, "loose"))
		pinfo->mipi.pixel_packing = 1;
	else
		pinfo->mipi.pixel_packing = 0;
	rc = mdss_panel_get_dst_fmt(pinfo->bpp,
		pinfo->mipi.mode, pinfo->mipi.pixel_packing,
		&(pinfo->mipi.dst_format));
	if (rc) {
		pr_debug("%s: problem determining dst format. Set Default\n",
			__func__);
		pinfo->mipi.dst_format =
			DSI_VIDEO_DST_FORMAT_RGB888;
	}
	pdest = of_get_property(np,
		"qcom,mdss-dsi-panel-destination", NULL);

	rc = of_property_read_u32(np,
		"qcom,mdss-dsi-underflow-color", &tmp);
	pinfo->lcdc.underflow_clr = (!rc ? tmp : 0xff);
	rc = of_property_read_u32(np,
		"qcom,mdss-dsi-border-color", &tmp);
	pinfo->lcdc.border_clr = (!rc ? tmp : 0);
	data = of_get_property(np, "qcom,mdss-dsi-panel-orientation", NULL);
	if (data) {
		pr_debug("panel orientation is %s\n", data);
		if (!strcmp(data, "180"))
			pinfo->panel_orientation = MDP_ROT_180;
		else if (!strcmp(data, "hflip"))
			pinfo->panel_orientation = MDP_FLIP_LR;
		else if (!strcmp(data, "vflip"))
			pinfo->panel_orientation = MDP_FLIP_UD;
	}

	rc = of_property_read_u32(np, "qcom,mdss-brightness-max-level", &tmp);
	pinfo->brightness_max = (!rc ? tmp : MDSS_MAX_BL_BRIGHTNESS);
	rc = of_property_read_u32(np, "qcom,mdss-dsi-bl-min-level", &tmp);
	pinfo->bl_min = (!rc ? tmp : 0);
	rc = of_property_read_u32(np, "qcom,mdss-dsi-bl-max-level", &tmp);
	pinfo->bl_max = (!rc ? tmp : 255);
	ctrl_pdata->bklt_max = pinfo->bl_max;

	rc = of_property_read_u32(np, "qcom,mdss-dsi-interleave-mode", &tmp);
	pinfo->mipi.interleave_mode = (!rc ? tmp : 0);

	pinfo->mipi.vsync_enable = of_property_read_bool(np,
		"qcom,mdss-dsi-te-check-enable");

	if (pinfo->sim_panel_mode == SIM_SW_TE_MODE)
		pinfo->mipi.hw_vsync_mode = false;
	else
		pinfo->mipi.hw_vsync_mode = of_property_read_bool(np,
			"qcom,mdss-dsi-te-using-te-pin");

	rc = of_property_read_u32(np,
		"qcom,mdss-dsi-h-sync-pulse", &tmp);
	pinfo->mipi.pulse_mode_hsa_he = (!rc ? tmp : false);

	pinfo->mipi.hfp_power_stop = of_property_read_bool(np,
		"qcom,mdss-dsi-hfp-power-mode");
	pinfo->mipi.hsa_power_stop = of_property_read_bool(np,
		"qcom,mdss-dsi-hsa-power-mode");
	pinfo->mipi.hbp_power_stop = of_property_read_bool(np,
		"qcom,mdss-dsi-hbp-power-mode");
	pinfo->mipi.last_line_interleave_en = of_property_read_bool(np,
		"qcom,mdss-dsi-last-line-interleave");
	pinfo->mipi.bllp_power_stop = of_property_read_bool(np,
		"qcom,mdss-dsi-bllp-power-mode");
	pinfo->mipi.eof_bllp_power_stop = of_property_read_bool(
		np, "qcom,mdss-dsi-bllp-eof-power-mode");
	pinfo->mipi.traffic_mode = DSI_NON_BURST_SYNCH_PULSE;
	data = of_get_property(np, "qcom,mdss-dsi-traffic-mode", NULL);
	if (data) {
		if (!strcmp(data, "non_burst_sync_event"))
			pinfo->mipi.traffic_mode = DSI_NON_BURST_SYNCH_EVENT;
		else if (!strcmp(data, "burst_mode"))
			pinfo->mipi.traffic_mode = DSI_BURST_MODE;
	}
	rc = of_property_read_u32(np,
		"qcom,mdss-dsi-te-dcs-command", &tmp);
	pinfo->mipi.insert_dcs_cmd =
			(!rc ? tmp : 1);
	rc = of_property_read_u32(np,
		"qcom,mdss-dsi-wr-mem-continue", &tmp);
	pinfo->mipi.wr_mem_continue =
			(!rc ? tmp : 0x3c);
	rc = of_property_read_u32(np,
		"qcom,mdss-dsi-wr-mem-start", &tmp);
	pinfo->mipi.wr_mem_start =
			(!rc ? tmp : 0x2c);
	rc = of_property_read_u32(np,
		"qcom,mdss-dsi-te-pin-select", &tmp);
	pinfo->mipi.te_sel =
			(!rc ? tmp : 1);
	rc = of_property_read_u32(np, "qcom,mdss-dsi-virtual-channel-id", &tmp);
	pinfo->mipi.vc = (!rc ? tmp : 0);
	pinfo->mipi.rgb_swap = DSI_RGB_SWAP_RGB;
	data = of_get_property(np, "qcom,mdss-dsi-color-order", NULL);
	if (data) {
		if (!strcmp(data, "rgb_swap_rbg"))
			pinfo->mipi.rgb_swap = DSI_RGB_SWAP_RBG;
		else if (!strcmp(data, "rgb_swap_bgr"))
			pinfo->mipi.rgb_swap = DSI_RGB_SWAP_BGR;
		else if (!strcmp(data, "rgb_swap_brg"))
			pinfo->mipi.rgb_swap = DSI_RGB_SWAP_BRG;
		else if (!strcmp(data, "rgb_swap_grb"))
			pinfo->mipi.rgb_swap = DSI_RGB_SWAP_GRB;
		else if (!strcmp(data, "rgb_swap_gbr"))
			pinfo->mipi.rgb_swap = DSI_RGB_SWAP_GBR;
	}
	pinfo->mipi.data_lane0 = of_property_read_bool(np,
		"qcom,mdss-dsi-lane-0-state");
	pinfo->mipi.data_lane1 = of_property_read_bool(np,
		"qcom,mdss-dsi-lane-1-state");
	pinfo->mipi.data_lane2 = of_property_read_bool(np,
		"qcom,mdss-dsi-lane-2-state");
	pinfo->mipi.data_lane3 = of_property_read_bool(np,
		"qcom,mdss-dsi-lane-3-state");

	rc = mdss_panel_parse_display_timings(np, &ctrl_pdata->panel_data);
	if (rc)
		return rc;

	pinfo->mipi.rx_eot_ignore = of_property_read_bool(np,
		"qcom,mdss-dsi-rx-eot-ignore");
	pinfo->mipi.tx_eot_append = of_property_read_bool(np,
		"qcom,mdss-dsi-tx-eot-append");

	rc = of_property_read_u32(np, "qcom,mdss-dsi-stream", &tmp);
	pinfo->mipi.stream = (!rc ? tmp : 0);

	data = of_get_property(np, "qcom,mdss-dsi-panel-mode-gpio-state", NULL);
	if (data) {
		if (!strcmp(data, "high"))
			pinfo->mode_gpio_state = MODE_GPIO_HIGH;
		else if (!strcmp(data, "low"))
			pinfo->mode_gpio_state = MODE_GPIO_LOW;
	} else {
		pinfo->mode_gpio_state = MODE_GPIO_NOT_VALID;
	}

	rc = of_property_read_u32(np, "qcom,mdss-mdp-transfer-time-us", &tmp);
	pinfo->mdp_transfer_time_us = (!rc ? tmp : DEFAULT_MDP_TRANSFER_TIME);

	pinfo->mipi.lp11_init = of_property_read_bool(np,
					"qcom,mdss-dsi-lp11-init");
	rc = of_property_read_u32(np, "qcom,mdss-dsi-init-delay-us", &tmp);
	pinfo->mipi.init_delay = (!rc ? tmp : 0);

	rc = of_property_read_u32(np, "qcom,mdss-dsi-post-init-delay", &tmp);
	pinfo->mipi.post_init_delay = (!rc ? tmp : 0);

	mdss_dsi_parse_trigger(np, &(pinfo->mipi.mdp_trigger),
		"qcom,mdss-dsi-mdp-trigger");

	mdss_dsi_parse_trigger(np, &(pinfo->mipi.dma_trigger),
		"qcom,mdss-dsi-dma-trigger");

	mdss_dsi_parse_reset_seq(np, pinfo->rst_seq, &(pinfo->rst_seq_len),
		"qcom,mdss-dsi-reset-sequence");

	mdss_dsi_parse_dcs_cmds(np, &ctrl_pdata->off_cmds,
		"qcom,mdss-dsi-off-command", "qcom,mdss-dsi-off-command-state");

	rc = of_property_read_u32(np, "qcom,adjust-timer-wakeup-ms", &tmp);
	pinfo->adjust_timer_delay_ms = (!rc ? tmp : 0);

	pinfo->mipi.force_clk_lane_hs = of_property_read_bool(np,
		"qcom,mdss-dsi-force-clock-lane-hs");

	rc = mdss_dsi_parse_panel_features(np, ctrl_pdata);
	if (rc) {
		pr_err("%s: failed to parse panel features\n", __func__);
		goto error;
	}

	mdss_dsi_parse_panel_horizintal_line_idle(np, ctrl_pdata);

	mdss_dsi_parse_dfps_config(np, ctrl_pdata);

	pinfo->is_dba_panel = of_property_read_bool(np,
			"qcom,dba-panel");

	if (pinfo->is_dba_panel) {
		bridge_chip_name = of_get_property(np,
			"qcom,bridge-name", &len);
		if (!bridge_chip_name || len <= 0) {
			pr_err("%s:%d Unable to read qcom,bridge_name, data=%pK,len=%d\n",
				__func__, __LINE__, bridge_chip_name, len);
			rc = -EINVAL;
			goto error;
		}
		strlcpy(ctrl_pdata->bridge_name, bridge_chip_name,
			MSM_DBA_CHIP_NAME_MAX_LEN);
	}

	return 0;

error:
	return -EINVAL;
}

int mdss_dsi_panel_init(struct device_node *node,
	struct mdss_dsi_ctrl_pdata *ctrl_pdata,
	int ndx)
{
	int rc = 0;
	static const char *panel_name;
	struct mdss_panel_info *pinfo;

	if (!node || !ctrl_pdata) {
		pr_err("%s: Invalid arguments\n", __func__);
		return -ENODEV;
	}

	pinfo = &ctrl_pdata->panel_data.panel_info;

	pr_debug("%s:%d\n", __func__, __LINE__);
	pinfo->panel_name[0] = '\0';
	panel_name = of_get_property(node, "qcom,mdss-dsi-panel-name", NULL);
	if (!panel_name) {
		pr_info("%s:%d, Panel name not specified\n",
						__func__, __LINE__);
	} else {
		pr_info("%s: Panel Name = %s\n", __func__, panel_name);
		strlcpy(&pinfo->panel_name[0], panel_name, MDSS_MAX_PANEL_LEN);
	}
	rc = mdss_panel_parse_dt(node, ctrl_pdata);
	if (rc) {
		pr_err("%s:%d panel dt parse failed\n", __func__, __LINE__);
		return rc;
	}

	pinfo->dynamic_switch_pending = false;
	pinfo->is_lpm_mode = false;
	pinfo->esd_rdy = false;

	ctrl_pdata->on = mdss_dsi_panel_on;
	ctrl_pdata->post_panel_on = mdss_dsi_post_panel_on;
	ctrl_pdata->off = mdss_dsi_panel_off;
	ctrl_pdata->low_power_config = mdss_dsi_panel_low_power_config;
	ctrl_pdata->panel_data.set_backlight = mdss_dsi_panel_bl_ctrl;
	ctrl_pdata->switch_mode = mdss_dsi_panel_switch_mode;
	ctrl_pdata->panel_data.vr_mode_enable = mdss_dsi_panel_enable_R_AID;

	if(zte_display_init==0)
	{
      mutex_init(&zte_display_lock);
	  zte_display_init=1;
	}
#ifdef ZTE_SAMSUNG_ACL_HBM
   samsung_panel_proc_init(ctrl_pdata);
#endif

	

	mdss_dsi_panel_lcd_proc(node);
	return 0;
}
