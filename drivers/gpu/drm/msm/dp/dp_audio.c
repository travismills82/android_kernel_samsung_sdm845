/*
 * Copyright (c) 2016-2017, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt)	"[drm-dp] %s: " fmt, __func__

#include <linux/of_platform.h>
#include <linux/msm_ext_display.h>

#include <drm/drm_dp_helper.h>

#ifdef CONFIG_SEC_DISPLAYPORT
#include "secdp.h"
#ifdef CONFIG_SEC_DISPLAYPORT_BIGDATA
#include <linux/displayport_bigdata.h>
#endif
#endif

#include "dp_catalog.h"
#include "dp_audio.h"
#include "dp_panel.h"

struct dp_audio_private {
	struct platform_device *ext_pdev;
	struct platform_device *pdev;
	struct dp_catalog_audio *catalog;
	struct msm_ext_disp_init_data ext_audio_data;
	struct dp_panel *panel;

	bool ack_enabled;
	bool session_on;
	bool engine_on;

	u32 channels;

	struct completion hpd_comp;
	struct workqueue_struct *notify_workqueue;
	struct delayed_work notify_delayed_work;

	struct dp_audio dp_audio;
};

static u32 dp_audio_get_header(struct dp_catalog_audio *catalog,
		enum dp_catalog_audio_sdp_type sdp,
		enum dp_catalog_audio_header_type header)
{
	catalog->sdp_type = sdp;
	catalog->sdp_header = header;
	catalog->get_header(catalog);

	return catalog->data;
}

static void dp_audio_set_header(struct dp_catalog_audio *catalog,
		u32 data,
		enum dp_catalog_audio_sdp_type sdp,
		enum dp_catalog_audio_header_type header)
{
	catalog->sdp_type = sdp;
	catalog->sdp_header = header;
	catalog->data = data;
	catalog->set_header(catalog);
}

static void dp_audio_stream_sdp(struct dp_audio_private *audio)
{
	struct dp_catalog_audio *catalog = audio->catalog;
	u32 value, new_value;
	u8 parity_byte;

	/* Config header and parity byte 1 */
	value = dp_audio_get_header(catalog,
			DP_AUDIO_SDP_STREAM, DP_AUDIO_SDP_HEADER_1);
	value &= 0x0000ffff;

	new_value = 0x02;
	parity_byte = dp_header_get_parity(new_value);
	value |= ((new_value << HEADER_BYTE_1_BIT)
			| (parity_byte << PARITY_BYTE_1_BIT));
	pr_debug("Header Byte 1: value = 0x%x, parity_byte = 0x%x\n",
			value, parity_byte);
	dp_audio_set_header(catalog, value,
		DP_AUDIO_SDP_STREAM, DP_AUDIO_SDP_HEADER_1);

	/* Config header and parity byte 2 */
	value = dp_audio_get_header(catalog,
			DP_AUDIO_SDP_STREAM, DP_AUDIO_SDP_HEADER_2);
	value &= 0xffff0000;
	new_value = 0x0;
	parity_byte = dp_header_get_parity(new_value);
	value |= ((new_value << HEADER_BYTE_2_BIT)
			| (parity_byte << PARITY_BYTE_2_BIT));
	pr_debug("Header Byte 2: value = 0x%x, parity_byte = 0x%x\n",
			value, parity_byte);

	dp_audio_set_header(catalog, value,
		DP_AUDIO_SDP_STREAM, DP_AUDIO_SDP_HEADER_2);

	/* Config header and parity byte 3 */
	value = dp_audio_get_header(catalog,
			DP_AUDIO_SDP_STREAM, DP_AUDIO_SDP_HEADER_3);
	value &= 0x0000ffff;

	new_value = audio->channels - 1;
	parity_byte = dp_header_get_parity(new_value);
	value |= ((new_value << HEADER_BYTE_3_BIT)
			| (parity_byte << PARITY_BYTE_3_BIT));
	pr_debug("Header Byte 3: value = 0x%x, parity_byte = 0x%x\n",
		value, parity_byte);

	dp_audio_set_header(catalog, value,
		DP_AUDIO_SDP_STREAM, DP_AUDIO_SDP_HEADER_3);
}

static void dp_audio_timestamp_sdp(struct dp_audio_private *audio)
{
	struct dp_catalog_audio *catalog = audio->catalog;
	u32 value, new_value;
	u8 parity_byte;

	/* Config header and parity byte 1 */
	value = dp_audio_get_header(catalog,
			DP_AUDIO_SDP_TIMESTAMP, DP_AUDIO_SDP_HEADER_1);
	value &= 0x0000ffff;

	new_value = 0x1;
	parity_byte = dp_header_get_parity(new_value);
	value |= ((new_value << HEADER_BYTE_1_BIT)
			| (parity_byte << PARITY_BYTE_1_BIT));
	pr_debug("Header Byte 1: value = 0x%x, parity_byte = 0x%x\n",
		value, parity_byte);
	dp_audio_set_header(catalog, value,
		DP_AUDIO_SDP_TIMESTAMP, DP_AUDIO_SDP_HEADER_1);

	/* Config header and parity byte 2 */
	value = dp_audio_get_header(catalog,
			DP_AUDIO_SDP_TIMESTAMP, DP_AUDIO_SDP_HEADER_2);
	value &= 0xffff0000;

	new_value = 0x17;
	parity_byte = dp_header_get_parity(new_value);
	value |= ((new_value << HEADER_BYTE_2_BIT)
			| (parity_byte << PARITY_BYTE_2_BIT));
	pr_debug("Header Byte 2: value = 0x%x, parity_byte = 0x%x\n",
			value, parity_byte);
	dp_audio_set_header(catalog, value,
		DP_AUDIO_SDP_TIMESTAMP, DP_AUDIO_SDP_HEADER_2);

	/* Config header and parity byte 3 */
	value = dp_audio_get_header(catalog,
			DP_AUDIO_SDP_TIMESTAMP, DP_AUDIO_SDP_HEADER_3);
	value &= 0x0000ffff;

	new_value = (0x0 | (0x11 << 2));
	parity_byte = dp_header_get_parity(new_value);
	value |= ((new_value << HEADER_BYTE_3_BIT)
			| (parity_byte << PARITY_BYTE_3_BIT));
	pr_debug("Header Byte 3: value = 0x%x, parity_byte = 0x%x\n",
			value, parity_byte);
	dp_audio_set_header(catalog, value,
		DP_AUDIO_SDP_TIMESTAMP, DP_AUDIO_SDP_HEADER_3);
}

static void dp_audio_infoframe_sdp(struct dp_audio_private *audio)
{
	struct dp_catalog_audio *catalog = audio->catalog;
	u32 value, new_value;
	u8 parity_byte;

	/* Config header and parity byte 1 */
	value = dp_audio_get_header(catalog,
			DP_AUDIO_SDP_INFOFRAME, DP_AUDIO_SDP_HEADER_1);
	value &= 0x0000ffff;

	new_value = 0x84;
	parity_byte = dp_header_get_parity(new_value);
	value |= ((new_value << HEADER_BYTE_1_BIT)
			| (parity_byte << PARITY_BYTE_1_BIT));
	pr_debug("Header Byte 1: value = 0x%x, parity_byte = 0x%x\n",
			value, parity_byte);
	dp_audio_set_header(catalog, value,
		DP_AUDIO_SDP_INFOFRAME, DP_AUDIO_SDP_HEADER_1);

	/* Config header and parity byte 2 */
	value = dp_audio_get_header(catalog,
			DP_AUDIO_SDP_INFOFRAME, DP_AUDIO_SDP_HEADER_2);
	value &= 0xffff0000;

	new_value = 0x1b;
	parity_byte = dp_header_get_parity(new_value);
	value |= ((new_value << HEADER_BYTE_2_BIT)
			| (parity_byte << PARITY_BYTE_2_BIT));
	pr_debug("Header Byte 2: value = 0x%x, parity_byte = 0x%x\n",
			value, parity_byte);
	dp_audio_set_header(catalog, value,
		DP_AUDIO_SDP_INFOFRAME, DP_AUDIO_SDP_HEADER_2);

	/* Config header and parity byte 3 */
	value = dp_audio_get_header(catalog,
			DP_AUDIO_SDP_INFOFRAME, DP_AUDIO_SDP_HEADER_3);
	value &= 0x0000ffff;

	new_value = (0x0 | (0x11 << 2));
	parity_byte = dp_header_get_parity(new_value);
	value |= ((new_value << HEADER_BYTE_3_BIT)
			| (parity_byte << PARITY_BYTE_3_BIT));
	pr_debug("Header Byte 3: value = 0x%x, parity_byte = 0x%x\n",
			new_value, parity_byte);
	dp_audio_set_header(catalog, value,
		DP_AUDIO_SDP_INFOFRAME, DP_AUDIO_SDP_HEADER_3);
}

static void dp_audio_copy_management_sdp(struct dp_audio_private *audio)
{
	struct dp_catalog_audio *catalog = audio->catalog;
	u32 value, new_value;
	u8 parity_byte;

	/* Config header and parity byte 1 */
	value = dp_audio_get_header(catalog,
			DP_AUDIO_SDP_COPYMANAGEMENT, DP_AUDIO_SDP_HEADER_1);
	value &= 0x0000ffff;

	new_value = 0x05;
	parity_byte = dp_header_get_parity(new_value);
	value |= ((new_value << HEADER_BYTE_1_BIT)
			| (parity_byte << PARITY_BYTE_1_BIT));
	pr_debug("Header Byte 1: value = 0x%x, parity_byte = 0x%x\n",
			value, parity_byte);
	dp_audio_set_header(catalog, value,
		DP_AUDIO_SDP_COPYMANAGEMENT, DP_AUDIO_SDP_HEADER_1);

	/* Config header and parity byte 2 */
	value = dp_audio_get_header(catalog,
			DP_AUDIO_SDP_COPYMANAGEMENT, DP_AUDIO_SDP_HEADER_2);
	value &= 0xffff0000;

	new_value = 0x0F;
	parity_byte = dp_header_get_parity(new_value);
	value |= ((new_value << HEADER_BYTE_2_BIT)
			| (parity_byte << PARITY_BYTE_2_BIT));
	pr_debug("Header Byte 2: value = 0x%x, parity_byte = 0x%x\n",
			value, parity_byte);
	dp_audio_set_header(catalog, value,
		DP_AUDIO_SDP_COPYMANAGEMENT, DP_AUDIO_SDP_HEADER_2);

	/* Config header and parity byte 3 */
	value = dp_audio_get_header(catalog,
			DP_AUDIO_SDP_COPYMANAGEMENT, DP_AUDIO_SDP_HEADER_3);
	value &= 0x0000ffff;

	new_value = 0x0;
	parity_byte = dp_header_get_parity(new_value);
	value |= ((new_value << HEADER_BYTE_3_BIT)
			| (parity_byte << PARITY_BYTE_3_BIT));
	pr_debug("Header Byte 3: value = 0x%x, parity_byte = 0x%x\n",
			value, parity_byte);
	dp_audio_set_header(catalog, value,
		DP_AUDIO_SDP_COPYMANAGEMENT, DP_AUDIO_SDP_HEADER_3);
}

static void dp_audio_isrc_sdp(struct dp_audio_private *audio)
{
	struct dp_catalog_audio *catalog = audio->catalog;
	u32 value, new_value;
	u8 parity_byte;

	/* Config header and parity byte 1 */
	value = dp_audio_get_header(catalog,
			DP_AUDIO_SDP_ISRC, DP_AUDIO_SDP_HEADER_1);
	value &= 0x0000ffff;

	new_value = 0x06;
	parity_byte = dp_header_get_parity(new_value);
	value |= ((new_value << HEADER_BYTE_1_BIT)
			| (parity_byte << PARITY_BYTE_1_BIT));
	pr_debug("Header Byte 1: value = 0x%x, parity_byte = 0x%x\n",
			value, parity_byte);
	dp_audio_set_header(catalog, value,
		DP_AUDIO_SDP_ISRC, DP_AUDIO_SDP_HEADER_1);

	/* Config header and parity byte 2 */
	value = dp_audio_get_header(catalog,
			DP_AUDIO_SDP_ISRC, DP_AUDIO_SDP_HEADER_2);
	value &= 0xffff0000;

	new_value = 0x0F;
	parity_byte = dp_header_get_parity(new_value);
	value |= ((new_value << HEADER_BYTE_2_BIT)
			| (parity_byte << PARITY_BYTE_2_BIT));
	pr_debug("Header Byte 2: value = 0x%x, parity_byte = 0x%x\n",
			value, parity_byte);
	dp_audio_set_header(catalog, value,
		DP_AUDIO_SDP_ISRC, DP_AUDIO_SDP_HEADER_2);
}

static void dp_audio_setup_sdp(struct dp_audio_private *audio)
{
	audio->catalog->config_sdp(audio->catalog);

	dp_audio_stream_sdp(audio);
	dp_audio_timestamp_sdp(audio);
	dp_audio_infoframe_sdp(audio);
	dp_audio_copy_management_sdp(audio);
	dp_audio_isrc_sdp(audio);
}

static void dp_audio_setup_acr(struct dp_audio_private *audio)
{
	u32 select = 0;
	struct dp_catalog_audio *catalog = audio->catalog;

	switch (audio->dp_audio.bw_code) {
	case DP_LINK_BW_1_62:
		select = 0;
		break;
	case DP_LINK_BW_2_7:
		select = 1;
		break;
	case DP_LINK_BW_5_4:
		select = 2;
		break;
	case DP_LINK_BW_8_1:
		select = 3;
		break;
	default:
		pr_debug("Unknown link rate\n");
		select = 0;
		break;
	}

	catalog->data = select;
	catalog->config_acr(catalog);
}

static void dp_audio_safe_to_exit_level(struct dp_audio_private *audio)
{
	struct dp_catalog_audio *catalog = audio->catalog;
	u32 safe_to_exit_level = 0;

	switch (audio->dp_audio.lane_count) {
	case 1:
		safe_to_exit_level = 14;
		break;
	case 2:
		safe_to_exit_level = 8;
		break;
	case 4:
		safe_to_exit_level = 5;
		break;
	default:
		pr_debug("setting the default safe_to_exit_level = %u\n",
				safe_to_exit_level);
		safe_to_exit_level = 14;
		break;
	}

	catalog->data = safe_to_exit_level;
	catalog->safe_to_exit_level(catalog);
}

static void dp_audio_enable(struct dp_audio_private *audio, bool enable)
{
	struct dp_catalog_audio *catalog = audio->catalog;

	catalog->data = enable;
	catalog->enable(catalog);

	audio->engine_on = enable;
}

static struct dp_audio_private *dp_audio_get_data(struct platform_device *pdev)
{
	struct msm_ext_disp_data *ext_data;
	struct dp_audio *dp_audio;

	if (!pdev) {
		pr_err("invalid input\n");
		return ERR_PTR(-ENODEV);
	}

	ext_data = platform_get_drvdata(pdev);
	if (!ext_data) {
		pr_err("invalid ext disp data\n");
		return ERR_PTR(-EINVAL);
	}

	dp_audio = ext_data->intf_data;
	if (!ext_data) {
		pr_err("invalid intf data\n");
		return ERR_PTR(-EINVAL);
	}

	return container_of(dp_audio, struct dp_audio_private, dp_audio);
}

static int dp_audio_info_setup(struct platform_device *pdev,
	struct msm_ext_disp_audio_setup_params *params)
{
	int rc = 0;
	struct dp_audio_private *audio;

	audio = dp_audio_get_data(pdev);
	if (IS_ERR(audio)) {
		rc = PTR_ERR(audio);
		goto end;
	}

#ifdef CONFIG_SEC_DISPLAYPORT_BIGDATA
	secdp_bigdata_save_item(BD_AUD_CH, params->num_of_channels);
	secdp_bigdata_save_item(BD_AUD_FREQ, params->sample_rate_hz);
#endif

	mutex_lock(&audio->dp_audio.ops_lock);

	audio->channels = params->num_of_channels;

	dp_audio_setup_sdp(audio);
	dp_audio_setup_acr(audio);
	dp_audio_safe_to_exit_level(audio);
	dp_audio_enable(audio, true);

	mutex_unlock(&audio->dp_audio.ops_lock);
end:
	return rc;
}

static int dp_audio_get_edid_blk(struct platform_device *pdev,
		struct msm_ext_disp_audio_edid_blk *blk)
{
	int rc = 0;
	struct dp_audio_private *audio;
	struct sde_edid_ctrl *edid;

	audio = dp_audio_get_data(pdev);
	if (IS_ERR(audio)) {
		rc = PTR_ERR(audio);
		goto end;
	}

	if (!audio->panel || !audio->panel->edid_ctrl) {
		pr_err("invalid panel data\n");
		rc = -EINVAL;
		goto end;
	}

	edid = audio->panel->edid_ctrl;

	blk->audio_data_blk = edid->audio_data_block;
	blk->audio_data_blk_size = edid->adb_size;

	blk->spk_alloc_data_blk = edid->spkr_alloc_data_block;
	blk->spk_alloc_data_blk_size = edid->sadb_size;
end:
	return rc;
}

static int dp_audio_get_cable_status(struct platform_device *pdev, u32 vote)
{
	int rc = 0;
	struct dp_audio_private *audio;

	audio = dp_audio_get_data(pdev);
	if (IS_ERR(audio)) {
		rc = PTR_ERR(audio);
		goto end;
	}

	return audio->session_on;
end:
	return rc;
}

static int dp_audio_get_intf_id(struct platform_device *pdev)
{
	int rc = 0;
	struct dp_audio_private *audio;

	audio = dp_audio_get_data(pdev);
	if (IS_ERR(audio)) {
		rc = PTR_ERR(audio);
		goto end;
	}

	return EXT_DISPLAY_TYPE_DP;
end:
	return rc;
}

static void dp_audio_teardown_done(struct platform_device *pdev)
{
	struct dp_audio_private *audio;

	audio = dp_audio_get_data(pdev);
	if (IS_ERR(audio))
		return;

	mutex_lock(&audio->dp_audio.ops_lock);
	dp_audio_enable(audio, false);
	mutex_unlock(&audio->dp_audio.ops_lock);

	complete_all(&audio->hpd_comp);

	pr_debug("audio engine disabled\n");
}

static int dp_audio_ack_done(struct platform_device *pdev, u32 ack)
{
	int rc = 0, ack_hpd;
	struct dp_audio_private *audio;

	audio = dp_audio_get_data(pdev);
	if (IS_ERR(audio)) {
		rc = PTR_ERR(audio);
		goto end;
	}

	if (ack & AUDIO_ACK_SET_ENABLE) {
		audio->ack_enabled = ack & AUDIO_ACK_ENABLE ?
			true : false;

		pr_debug("audio ack feature %s\n",
			audio->ack_enabled ? "enabled" : "disabled");
		goto end;
	}

	if (!audio->ack_enabled)
		goto end;

	ack_hpd = ack & AUDIO_ACK_CONNECT;

	pr_debug("acknowledging audio (%d)\n", ack_hpd);

	if (!audio->engine_on)
		complete_all(&audio->hpd_comp);
end:
	return rc;
}

static int dp_audio_codec_ready(struct platform_device *pdev)
{
	int rc = 0;
	struct dp_audio_private *audio;

	audio = dp_audio_get_data(pdev);
	if (IS_ERR(audio)) {
		pr_err("invalid input\n");
		rc = PTR_ERR(audio);
		goto end;
	}

	queue_delayed_work(audio->notify_workqueue,
			&audio->notify_delayed_work, HZ/4);
end:
	return rc;
}

static int dp_audio_init_ext_disp(struct dp_audio_private *audio)
{
	int rc = 0;
	struct device_node *pd = NULL;
	const char *phandle = "qcom,ext-disp";
	struct msm_ext_disp_init_data *ext;
	struct msm_ext_disp_audio_codec_ops *ops;

	ext = &audio->ext_audio_data;
	ops = &ext->codec_ops;

	ext->type = EXT_DISPLAY_TYPE_DP;
	ext->pdev = audio->pdev;
	ext->intf_data = &audio->dp_audio;

	ops->audio_info_setup   = dp_audio_info_setup;
	ops->get_audio_edid_blk = dp_audio_get_edid_blk;
	ops->cable_status       = dp_audio_get_cable_status;
	ops->get_intf_id        = dp_audio_get_intf_id;
	ops->teardown_done      = dp_audio_teardown_done;
	ops->acknowledge        = dp_audio_ack_done;
	ops->ready              = dp_audio_codec_ready;

	if (!audio->pdev->dev.of_node) {
		pr_err("cannot find audio dev.of_node\n");
		rc = -ENODEV;
		goto end;
	}

	pd = of_parse_phandle(audio->pdev->dev.of_node, phandle, 0);
	if (!pd) {
		pr_err("cannot parse %s handle\n", phandle);
		rc = -ENODEV;
		goto end;
	}

	audio->ext_pdev = of_find_device_by_node(pd);
	if (!audio->ext_pdev) {
		pr_err("cannot find %s pdev\n", phandle);
		rc = -ENODEV;
		goto end;
	}

	rc = msm_ext_disp_register_intf(audio->ext_pdev, ext);
	if (rc)
		pr_err("failed to register disp\n");
end:
	if (pd)
		of_node_put(pd);

	return rc;
}

static int dp_audio_notify(struct dp_audio_private *audio, u32 state)
{
	int rc = 0;
	struct msm_ext_disp_init_data *ext = &audio->ext_audio_data;

	rc = ext->intf_ops.audio_notify(audio->ext_pdev,
			EXT_DISPLAY_TYPE_DP, state);
	if (rc) {
		pr_err("failed to notify audio. state=%d err=%d\n", state, rc);
		goto end;
	}

#ifdef CONFIG_SEC_DISPLAYPORT
	if (state == EXT_DISPLAY_CABLE_DISCONNECT) {
#endif
	reinit_completion(&audio->hpd_comp);
	rc = wait_for_completion_timeout(&audio->hpd_comp, HZ * 5);
	if (!rc) {
		pr_err("timeout. state=%d err=%d\n", state, rc);
		rc = -ETIMEDOUT;
		goto end;
	}
#ifdef CONFIG_SEC_DISPLAYPORT
	}
#endif
	pr_debug("success\n");
end:
	return rc;
}

static int dp_audio_on(struct dp_audio *dp_audio)
{
	int rc = 0;
	struct dp_audio_private *audio;
	struct msm_ext_disp_init_data *ext;

	if (!dp_audio) {
		pr_err("invalid input\n");
		return -EINVAL;
	}

	audio = container_of(dp_audio, struct dp_audio_private, dp_audio);
	if (IS_ERR(audio)) {
		pr_err("invalid input\n");
		return -EINVAL;
	}

	ext = &audio->ext_audio_data;

	audio->session_on = true;

	rc = ext->intf_ops.audio_config(audio->ext_pdev,
			EXT_DISPLAY_TYPE_DP,
			EXT_DISPLAY_CABLE_CONNECT);
	if (rc) {
		pr_err("failed to config audio, err=%d\n", rc);
		goto end;
	}

	rc = dp_audio_notify(audio, EXT_DISPLAY_CABLE_CONNECT);
	if (rc)
		goto end;

	pr_debug("success\n");
end:
	return rc;
}

static int dp_audio_off(struct dp_audio *dp_audio)
{
	int rc = 0;
	struct dp_audio_private *audio;
	struct msm_ext_disp_init_data *ext;
	bool work_pending = false;

	if (!dp_audio) {
		pr_err("invalid input\n");
		return -EINVAL;
	}

	audio = container_of(dp_audio, struct dp_audio_private, dp_audio);
	ext = &audio->ext_audio_data;

#ifdef CONFIG_SEC_DISPLAYPORT
	if (!audio->session_on) {
		pr_info("dp audio already off\n");
		return rc;
	}
#endif

	work_pending = cancel_delayed_work_sync(&audio->notify_delayed_work);
	if (work_pending)
		pr_debug("pending notification work completed\n");

	rc = dp_audio_notify(audio, EXT_DISPLAY_CABLE_DISCONNECT);
	if (rc)
		goto end;

	pr_debug("success\n");
end:
	rc = ext->intf_ops.audio_config(audio->ext_pdev,
			EXT_DISPLAY_TYPE_DP,
			EXT_DISPLAY_CABLE_DISCONNECT);
	if (rc)
		pr_err("failed to config audio, err=%d\n", rc);

	audio->session_on = false;
	audio->engine_on  = false;

	return rc;
}

static void dp_audio_notify_work_fn(struct work_struct *work)
{
	struct dp_audio_private *audio;
	struct delayed_work *dw = to_delayed_work(work);

	audio = container_of(dw, struct dp_audio_private, notify_delayed_work);

	dp_audio_notify(audio, EXT_DISPLAY_CABLE_CONNECT);
}

static int dp_audio_create_notify_workqueue(struct dp_audio_private *audio)
{
	audio->notify_workqueue = create_workqueue("sdm_dp_audio_notify");
	if (IS_ERR_OR_NULL(audio->notify_workqueue)) {
		pr_err("Error creating notify_workqueue\n");
		return -EPERM;
	}

	INIT_DELAYED_WORK(&audio->notify_delayed_work, dp_audio_notify_work_fn);

	return 0;
}

static void dp_audio_destroy_notify_workqueue(struct dp_audio_private *audio)
{
	if (audio->notify_workqueue)
		destroy_workqueue(audio->notify_workqueue);
}

struct dp_audio *dp_audio_get(struct platform_device *pdev,
			struct dp_panel *panel,
			struct dp_catalog_audio *catalog)
{
	int rc = 0;
	struct dp_audio_private *audio;
	struct dp_audio *dp_audio;

	if (!pdev || !panel || !catalog) {
		pr_err("invalid input\n");
		rc = -EINVAL;
		goto error;
	}

	audio = devm_kzalloc(&pdev->dev, sizeof(*audio), GFP_KERNEL);
	if (!audio) {
		rc = -ENOMEM;
		goto error;
	}

	rc = dp_audio_create_notify_workqueue(audio);
	if (rc)
		goto error_notify_workqueue;

	init_completion(&audio->hpd_comp);

	audio->pdev = pdev;
	audio->panel = panel;
	audio->catalog = catalog;

	dp_audio = &audio->dp_audio;

	mutex_init(&dp_audio->ops_lock);

	dp_audio->on  = dp_audio_on;
	dp_audio->off = dp_audio_off;

	rc = dp_audio_init_ext_disp(audio);
	if (rc) {
		goto error_ext_disp;
	}

	catalog->init(catalog);

	return dp_audio;
error_ext_disp:
	dp_audio_destroy_notify_workqueue(audio);
error_notify_workqueue:
	devm_kfree(&pdev->dev, audio);
error:
	return ERR_PTR(rc);
}

void dp_audio_put(struct dp_audio *dp_audio)
{
	struct dp_audio_private *audio;

	if (!dp_audio)
		return;

	audio = container_of(dp_audio, struct dp_audio_private, dp_audio);
	mutex_destroy(&dp_audio->ops_lock);

	dp_audio_destroy_notify_workqueue(audio);

	devm_kfree(&audio->pdev->dev, audio);
}
