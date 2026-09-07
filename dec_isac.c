/*
 *			GPAC - Multimedia Framework C SDK
 *
 *  This file is part of GPAC / iSAC decoder filter, built on the iSAC sources
 *  from WebRTC.
 *
 *  iSAC - internet Speech Audio Codec - is the wideband speech codec Global IP
 *  Solutions wrote for what became WebRTC. It codes 16 kHz or 32 kHz speech in
 *  30 ms frames at a rate the encoder adapts to the channel, which is why a
 *  frame carries no length of its own and the packets have to be delimited by
 *  whatever holds them.
 *
 *  There is no standalone iSAC library to build against: the codec was
 *  removed from WebRTC in 2022 and no distribution packages it. The sources
 *  here are the last consistent pairing of the codec with the signal
 *  processing routines it needs, taken from the m93 branch; see the README for
 *  the exact revision.
 *
 *  There is likewise no file format, because iSAC only ever travelled over
 *  RTP. The one container that exists is the bitstream dump WebRTC's own test
 *  program writes and reads: per frame, a big-endian 16-bit length followed by
 *  that many bytes, and nothing else - no magic, no header, no sample rate.
 *  That is what this filter reads, and it is why the .isac extension is the
 *  only thing identifying the stream and why the sampling rate is an option
 *  rather than something read from the file.
 */

#include <gpac/filters.h>
#include <gpac/constants.h>
#include <string.h>

#include <modules/audio_coding/codecs/isac/main/include/isac.h>

/* the encoder never emits more than this in one frame at 32 kHz / 56 kbit/s */
#define ISAC_MAX_PAYLOAD 1000
/* 30 ms at 32 kHz, the longest frame the codec produces */
#define ISAC_MAX_SAMPLES 960

typedef struct
{
	GF_FilterPid *ipid, *opid;
	u32 srate;
} GF_ISACDecCtx;

static GF_Err isacdec_configure_pid(GF_Filter *filter, GF_FilterPid *pid, Bool is_remove)
{
	GF_ISACDecCtx *ctx = (GF_ISACDecCtx *)gf_filter_get_udta(filter);

	if (is_remove)
	{
		if (ctx->opid)
		{
			gf_filter_pid_remove(ctx->opid);
			ctx->opid = NULL;
		}
		ctx->ipid = NULL;
		return GF_OK;
	}
	if (!gf_filter_pid_check_caps(pid))
		return GF_NOT_SUPPORTED;

	if ((ctx->srate != 16000) && (ctx->srate != 32000))
	{
		GF_LOG(GF_LOG_ERROR, GF_LOG_CODEC, ("[iSACDec] srate must be 16000 or 32000, got %u\n", ctx->srate));
		return GF_BAD_PARAM;
	}

	ctx->ipid = pid;
	gf_filter_pid_set_framing_mode(pid, GF_TRUE);

	if (!ctx->opid)
		ctx->opid = gf_filter_pid_new(filter);

	gf_filter_pid_copy_properties(ctx->opid, pid);
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_STREAM_TYPE, &PROP_UINT(GF_STREAM_AUDIO));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_CODECID, &PROP_UINT(GF_CODECID_RAW));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_AUDIO_FORMAT, &PROP_UINT(GF_AUDIO_FMT_S16));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_SAMPLE_RATE, &PROP_UINT(ctx->srate));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_TIMESCALE, &PROP_UINT(ctx->srate));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_NUM_CHANNELS, &PROP_UINT(1));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_CHANNEL_LAYOUT, &PROP_LONGUINT(GF_AUDIO_CH_FRONT_CENTER));

	return GF_OK;
}

static GF_Err isacdec_process(GF_Filter *filter)
{
	GF_FilterPacket *pck, *dst_pck;
	const u8 *data;
	u8 *output;
	u32 size, pos = 0, out_alloc, nb_samples = 0;
	ISACStruct *dec = NULL;
	GF_ISACDecCtx *ctx = (GF_ISACDecCtx *)gf_filter_get_udta(filter);

	pck = gf_filter_pid_get_packet(ctx->ipid);
	if (!pck)
	{
		if (gf_filter_pid_is_eos(ctx->ipid))
		{
			gf_filter_pid_set_eos(ctx->opid);
			return GF_EOS;
		}
		return GF_OK;
	}
	data = gf_filter_pck_get_data(pck, &size);
	if (!data || (size < 3))
	{
		gf_filter_pid_drop_packet(ctx->ipid);
		GF_LOG(GF_LOG_ERROR, GF_LOG_CODEC, ("[iSACDec] File too short to hold a frame\n"));
		return GF_NON_COMPLIANT_BITSTREAM;
	}

	if (WebRtcIsac_Create(&dec))
	{
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_OUT_OF_MEM;
	}
	WebRtcIsac_SetDecSampRate(dec, (u16)ctx->srate);
	WebRtcIsac_DecoderInit(dec);

	/* Every frame is 30 ms, and the shortest one is a couple of bytes, so the
	 * frame count bounds the sample count: allocate for the whole file at once
	 * and send it as a single packet, like the other whole-file speech
	 * decoders here. */
	out_alloc = (size / 3 + 1) * ISAC_MAX_SAMPLES * 2;
	dst_pck = gf_filter_pck_new_alloc(ctx->opid, out_alloc, &output);
	if (!dst_pck)
	{
		WebRtcIsac_Free(dec);
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_OUT_OF_MEM;
	}

	while (pos + 2 <= size)
	{
		u32 nb_bytes = ((u32)data[pos] << 8) | data[pos + 1];
		s16 speech_type = 0;
		int got;

		pos += 2;
		if (!nb_bytes || (nb_bytes > ISAC_MAX_PAYLOAD) || (pos + nb_bytes > size))
			break;
		if ((nb_samples + ISAC_MAX_SAMPLES) * 2 > out_alloc)
			break;

		got = WebRtcIsac_Decode(dec, data + pos, (size_t)nb_bytes,
		                        (s16 *)(output + (size_t)nb_samples * 2), &speech_type);
		if (got < 0)
		{
			/* iSAC frames are independent, but a frame that fails here means
			 * the framing is wrong rather than that one packet was damaged:
			 * the length prefix that led us to it came from the file itself. */
			GF_LOG(GF_LOG_WARNING, GF_LOG_CODEC, ("[iSACDec] Frame at offset %u did not decode, stopping\n", pos));
			break;
		}
		nb_samples += (u32)got;
		pos += nb_bytes;
	}
	WebRtcIsac_Free(dec);

	if (!nb_samples)
	{
		gf_filter_pck_discard(dst_pck);
		gf_filter_pid_drop_packet(ctx->ipid);
		GF_LOG(GF_LOG_ERROR, GF_LOG_CODEC, ("[iSACDec] No frame decoded - is this an iSAC bitstream dump, and is srate right?\n"));
		return GF_NON_COMPLIANT_BITSTREAM;
	}

	gf_filter_pck_truncate(dst_pck, nb_samples * 2);
	gf_filter_pck_set_cts(dst_pck, 0);
	gf_filter_pck_set_duration(dst_pck, nb_samples);
	gf_filter_pck_set_sap(dst_pck, GF_FILTER_SAP_1);
	gf_filter_pck_send(dst_pck);

	gf_filter_pid_drop_packet(ctx->ipid);
	gf_filter_pid_set_eos(ctx->opid);
	return GF_EOS;
}

static const GF_FilterCapability ISACDecCaps[] =
	{
		CAP_UINT(GF_CAPS_INPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_FILE),
		CAP_STRING(GF_CAPS_INPUT, GF_PROP_PID_FILE_EXT, "isac"),
		CAP_STRING(GF_CAPS_INPUT, GF_PROP_PID_MIME, "audio/isac|audio/x-isac"),
		CAP_UINT(GF_CAPS_OUTPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_AUDIO),
		CAP_UINT(GF_CAPS_OUTPUT, GF_PROP_PID_CODECID, GF_CODECID_RAW),
};

#define OFFS(_n) #_n, offsetof(GF_ISACDecCtx, _n)
static const GF_FilterArgs ISACDecArgs[] =
	{
		/* no min_max_enum here: a "|" list makes GPAC read the property as an enum
		   and resolve the default to its index, so "16000" would arrive as 0. The
		   two accepted values are checked in configure_pid instead. */
		{OFFS(srate), "sampling rate in Hz, 16000 (wideband) or 32000 (super-wideband); an iSAC bitstream dump does not carry it", GF_PROP_UINT, "16000", NULL, 0},
		{0}};

GF_FilterRegister ISACDecoderRegister = {
	.name = "isacdec",
	GF_FS_SET_DESCRIPTION("iSAC (internet Speech Audio Codec) decoder")
		GF_FS_SET_HELP("This filter decodes iSAC speech using WebRTC's iSAC sources. It reads the bitstream dump format of WebRTC's own test program - per frame, a big-endian 16-bit length then the payload - which is the only container iSAC ever had. The sampling rate is not in the file and is given by srate.")
			.private_size = sizeof(GF_ISACDecCtx),
	.args = ISACDecArgs,
	SETCAPS(ISACDecCaps),
	.configure_pid = isacdec_configure_pid,
	.process = isacdec_process,
};

const GF_FilterRegister *EMSCRIPTEN_KEEPALIVE isacdec_register(GF_FilterSession *session)
{
	return &ISACDecoderRegister;
}

#include "filter_register.h"
__attribute__((constructor))
void register_isacdec(void)
{
	gf_filter_auto_register("isacdec", isacdec_register);
}
