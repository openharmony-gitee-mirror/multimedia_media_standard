/*
 * Copyright (C) 2021 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "config.h"
#include "gst_surface_video_src.h"
#include <gst/gst.h>
#include "media_errors.h"
#include "video_capture_factory.h"

static GstStaticPadTemplate gst_video_src_template =
GST_STATIC_PAD_TEMPLATE("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS("video/x-h264, "
        "alignment=(string) au, "
        "framerate=(fraction)30/1, "
        "stream-format=(string) avc, "
        "pixel-aspect-ratio=(fraction)1/1, "
        "level=(string) 2, "
        "profile=(string) high, "
        "width =(int) [ 1, MAX ],"
        "height =(int) [ 1, MAX ]"));

constexpr VideoStreamType DEFAULT_STREAM_TYPE = VIDEO_STREAM_TYPE_UNKNOWN;

enum {
    PROP_0,
    PROP_STREAM_TYPE,
    PROP_SURFACE_WIDTH,
    PROP_SURFACE_HEIGHT,
    PROP_SURFACE,
};

using namespace OHOS::Media;

#define gst_surface_video_src_parent_class parent_class
G_DEFINE_TYPE(GstSurfaceVideoSrc, gst_surface_video_src, GST_TYPE_PUSH_SRC);

static void gst_surface_video_src_set_property(GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec);
static void gst_surface_video_src_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static GstStateChangeReturn gst_surface_video_src_change_state(GstElement *element, GstStateChange transition);
static GstFlowReturn gst_surface_video_src_create(GstPushSrc *psrc, GstBuffer **outbuf);
static void gst_surface_video_src_set_stream_type(GstSurfaceVideoSrc *src, gint stream_type);
static gboolean gst_surface_video_src_negotiate(GstBaseSrc *basesrc);
static gboolean gst_surface_video_src_is_seekable(GstBaseSrc *src);

#define GST_TYPE_SURFACE_VIDEO_SRC_STREAM_TYPE (gst_surface_video_src_stream_type_get_type())
static GType gst_surface_video_src_stream_type_get_type(void)
{
    static GType surface_video_src_stream_type = 0;
    static const GEnumValue stream_types[] = {
        {VIDEO_STREAM_TYPE_UNKNOWN, "UNKNOWN", "UNKNOWN"},
        {VIDEO_STREAM_TYPE_ES_AVC, "ES_AVC", "ES_AVC"},
        {VIDEO_STREAM_TYPE_YUV_420, "YUV_420", "YUV_420"},
        {0, nullptr, nullptr}
    };
    if (!surface_video_src_stream_type) {
        surface_video_src_stream_type = g_enum_register_static("VideoStreamType", stream_types);
    }
    return surface_video_src_stream_type;
}

static void gst_surface_video_src_class_init(GstSurfaceVideoSrcClass *klass)
{
    GObjectClass *gobject_class;
    GstElementClass *gstelement_class;
    GstBaseSrcClass *gstbasesrc_class;
    GstPushSrcClass *gstpushsrc_class;

    gobject_class = (GObjectClass *) klass;
    gstelement_class = (GstElementClass *) klass;
    gstbasesrc_class = (GstBaseSrcClass *) klass;
    gstpushsrc_class = (GstPushSrcClass *) klass;

    gobject_class->set_property = gst_surface_video_src_set_property;
    gobject_class->get_property = gst_surface_video_src_get_property;

    g_object_class_install_property(gobject_class, PROP_STREAM_TYPE,
        g_param_spec_enum("stream-type", "Stream type",
            "Stream type", GST_TYPE_SURFACE_VIDEO_SRC_STREAM_TYPE, DEFAULT_STREAM_TYPE,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_SURFACE_WIDTH,
        g_param_spec_uint("surface-width", "Surface width",
            "Surface width", 0, G_MAXINT32, 0,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_SURFACE_HEIGHT,
        g_param_spec_uint("surface-height", "Surface height",
            "Surface width", 0, G_MAXINT32, 0,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_SURFACE,
        g_param_spec_pointer("surface", "Surface", "Surface for recording",
            (GParamFlags)(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));

    gst_element_class_set_static_metadata(gstelement_class,
        "Surface video source", "Source/Video",
        "Retrieve video frame from surface buffer queue", "Harmony OS");

    gst_element_class_add_static_pad_template(gstelement_class, &gst_video_src_template);

    gstelement_class->change_state = gst_surface_video_src_change_state;

    gstbasesrc_class->negotiate = gst_surface_video_src_negotiate;
    gstbasesrc_class->is_seekable = gst_surface_video_src_is_seekable;

    gstpushsrc_class->create = gst_surface_video_src_create;
}

static void gst_surface_video_src_init(GstSurfaceVideoSrc *src)
{
    gst_base_src_set_format (GST_BASE_SRC(src), GST_FORMAT_TIME);
    gst_base_src_set_live (GST_BASE_SRC(src), TRUE);
    src->stream_type = VIDEO_STREAM_TYPE_ES_AVC;
    src->capture = nullptr;
    src->src_caps = nullptr;
    src->surface_width = 0;
    src->surface_height = 0;
    src->is_start = FALSE;
    src->need_codec_data = TRUE;
}

static void gst_surface_video_src_set_property(GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec)
{
    GstSurfaceVideoSrc *src = GST_SURFACE_VIDEO_SRC(object);
    switch (prop_id) {
        case PROP_STREAM_TYPE:
            gst_surface_video_src_set_stream_type(src, g_value_get_enum(value));
            break;
        case PROP_SURFACE_WIDTH:
            src->surface_width = g_value_get_uint(value);
            break;
        case PROP_SURFACE_HEIGHT:
            src->surface_height = g_value_get_uint(value);
            break;
        default:
            break;
    }
}

static void gst_surface_video_src_set_stream_type(GstSurfaceVideoSrc *src, gint stream_type)
{
    src->stream_type = VIDEO_STREAM_TYPE_ES_AVC;
    src->need_codec_data = TRUE;
}

static void gst_surface_video_src_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    GstSurfaceVideoSrc *src = GST_SURFACE_VIDEO_SRC(object);
    (void)pspec;
    switch (prop_id) {
        case PROP_STREAM_TYPE:
            g_value_set_enum(value, src->stream_type);
            break;
        case PROP_SURFACE_WIDTH:
            g_value_set_uint(value, src->surface_width);
            break;
        case PROP_SURFACE_HEIGHT:
            g_value_set_uint(value, src->surface_height);
            break;
        case PROP_SURFACE:
            g_return_if_fail(src->capture != nullptr);
            g_value_set_pointer(value, src->capture->GetSurface().GetRefPtr());
            break;
    }
}

static gboolean process_codec_data(GstSurfaceVideoSrc *src)
{
    g_return_val_if_fail(src->capture != nullptr, FALSE);

    std::shared_ptr<EsAvcCodecBuffer> codecBuffer = src->capture->GetCodecBuffer();
    g_return_val_if_fail(codecBuffer != nullptr, FALSE);

    if (codecBuffer->gstCodecBuffer == nullptr) {
        return FALSE;
    }

    if (src->src_caps != nullptr) {
        gst_caps_unref(src->src_caps);
    }

    src->src_caps = gst_caps_new_simple("video/x-h264",
        "width", G_TYPE_INT, codecBuffer->width,
        "height", G_TYPE_INT, codecBuffer->height,
        "framerate", GST_TYPE_FRACTION, 30, 1,
        "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
        "level", G_TYPE_STRING, "2",
        "profile", G_TYPE_STRING, "high",
        "alignment", G_TYPE_STRING, "au",
        "stream-format", G_TYPE_STRING, "avc", nullptr);
    gst_caps_set_simple(src->src_caps, "codec_data", GST_TYPE_BUFFER, codecBuffer->gstCodecBuffer, nullptr);
    GstBaseSrc *basesrc = GST_BASE_SRC_CAST(src);
    basesrc->segment.start = codecBuffer->segmentStart;
    gst_buffer_unref(codecBuffer->gstCodecBuffer);
    return TRUE;
}

static gboolean start_video_capture(GstSurfaceVideoSrc *src)
{
    g_return_val_if_fail(src->capture != nullptr, FALSE);

    g_return_val_if_fail(src->capture->Start() == MSERR_OK, FALSE);
    if (src->need_codec_data) {
        gboolean ret = process_codec_data(src);
        return ret;
    }
    return TRUE;
}

static GstStateChangeReturn gst_surface_video_src_change_state(GstElement *element, GstStateChange transition)
{
    GstSurfaceVideoSrc *src = GST_SURFACE_VIDEO_SRC(element);
    switch (transition) {
        case GST_STATE_CHANGE_NULL_TO_READY:
            src->capture = OHOS::Media::VideoCaptureFactory::CreateVideoCapture(src->stream_type);
            g_return_val_if_fail(src->capture != nullptr, GST_STATE_CHANGE_FAILURE);
            break;
        case GST_STATE_CHANGE_READY_TO_PAUSED:
            g_return_val_if_fail(src->capture != nullptr, GST_STATE_CHANGE_FAILURE);
            g_return_val_if_fail(src->capture->SetSurfaceHeight(src->surface_height) == MSERR_OK,
                GST_STATE_CHANGE_FAILURE);
            g_return_val_if_fail(src->capture->SetSurfaceWidth(src->surface_width) == MSERR_OK,
                GST_STATE_CHANGE_FAILURE);
            g_return_val_if_fail(src->capture->Prepare() == MSERR_OK, GST_STATE_CHANGE_FAILURE);
            break;
        case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
            g_return_val_if_fail(src->capture != nullptr, GST_STATE_CHANGE_FAILURE);
            if (src->is_start == FALSE) {
                g_return_val_if_fail(start_video_capture(src) == TRUE, GST_STATE_CHANGE_FAILURE);
                src->is_start = TRUE;
            } else {
                g_return_val_if_fail(src->capture->Resume() == MSERR_OK, GST_STATE_CHANGE_FAILURE);
            }
            break;
        default:
            break;
    }
    GstStateChangeReturn ret = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);

    switch (transition) {
        case GST_STATE_CHANGE_PAUSED_TO_READY:
            src->is_start = FALSE;
            g_return_val_if_fail(src->capture != nullptr, GST_STATE_CHANGE_FAILURE);
            g_return_val_if_fail(src->capture->Stop() == MSERR_OK, GST_STATE_CHANGE_FAILURE);
            break;
        case GST_STATE_CHANGE_READY_TO_NULL:
            src->capture = nullptr;
            break;
        default:
            break;
    }

    return ret;
}

static gboolean gst_surface_video_src_is_seekable(GstBaseSrc *basesrc)
{
    (void)basesrc;
    return FALSE;
}

static gboolean gst_surface_video_src_negotiate(GstBaseSrc *bsrc)
{
    gst_base_src_wait_playing(bsrc);
    GstSurfaceVideoSrc *src = GST_SURFACE_VIDEO_SRC(bsrc);
    return gst_base_src_set_caps(bsrc, src->src_caps);
}

static GstFlowReturn gst_surface_video_src_create(GstPushSrc *psrc, GstBuffer **outbuf)
{
    GstSurfaceVideoSrc *src = GST_SURFACE_VIDEO_SRC(psrc);
    if (src->is_start == FALSE) {
        return GST_FLOW_EOS;
    }
    g_return_val_if_fail(src->capture != nullptr, GST_FLOW_ERROR);

    std::shared_ptr<VideoFrameBuffer> frameBuffer = src->capture->GetFrameBuffer();
    g_return_val_if_fail(frameBuffer != nullptr, GST_FLOW_ERROR);

    gst_base_src_set_blocksize(GST_BASE_SRC_CAST(src), static_cast<guint>(frameBuffer->size));

    *outbuf = frameBuffer->gstBuffer;
    GST_BUFFER_PTS(*outbuf) = frameBuffer->timeStamp;
    GST_BUFFER_DURATION(*outbuf) = frameBuffer->duration;
    return GST_FLOW_OK;
}

static gboolean plugin_init(GstPlugin *plugin)
{
    return gst_element_register(plugin, "surfacevideosrc", GST_RANK_PRIMARY, GST_TYPE_SURFACE_VIDEO_SRC);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    _surface_video_src,
    "GStreamer Surface Video Source",
    plugin_init,
    PACKAGE_VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)