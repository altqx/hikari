/* Checks that the GStreamer a package runs with has what legacy HikariSub
 * uses: the elements playbin, appsrc, appsink, audioconvert, audioresample,
 * autoaudiosink and videoconvert, and, given a media file, the demuxer,
 * parsers and decoders to play it through to the end.
 *
 *   gst-check [file]
 *
 * Exits 0 when everything is there, 1 with the missing elements, 2 when the
 * file does not decode. A stand-in for the app's own --hikari-gst-check
 * switch until that exists; the container tests run it with the AppImage's
 * environment (through its AppRun) or against the host's plugins after a
 * native install. Built in the floor container, it links only GStreamer's
 * core and GLib, which every host provides.
 */
#include <gst/gst.h>
#include <stdio.h>
#include <string.h>

static const char *const elements[] = {
    "playbin", "appsrc", "appsink", "audioconvert",
    "audioresample", "autoaudiosink", "videoconvert",
};

static int decode(const char *path)
{
    GError *error = NULL;
    gchar *uri = gst_filename_to_uri(path, &error);
    if (!uri) {
        fprintf(stderr, "gst-check: %s\n", error->message);
        return 2;
    }
    GstElement *playbin = gst_element_factory_make("playbin", NULL);
    GstElement *video = gst_element_factory_make("fakesink", NULL);
    GstElement *audio = gst_element_factory_make("fakesink", NULL);
    g_object_set(video, "sync", FALSE, NULL);
    g_object_set(audio, "sync", FALSE, NULL);
    g_object_set(playbin, "uri", uri, "video-sink", video, "audio-sink", audio, NULL);
    g_free(uri);

    GstBus *bus = gst_element_get_bus(playbin);
    int result = 2;
    if (gst_element_set_state(playbin, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE) {
        for (;;) {
            GstMessage *message = gst_bus_timed_pop_filtered(bus, 30 * GST_SECOND,
                GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
            if (!message) {
                fprintf(stderr, "gst-check: no end of stream after 30 s\n");
                break;
            }
            if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
                GError *err = NULL;
                gchar *debug = NULL;
                gst_message_parse_error(message, &err, &debug);
                fprintf(stderr, "gst-check: decode failed: %s (%s)\n",
                        err->message, debug ? debug : "");
                g_clear_error(&err);
                g_free(debug);
                gst_message_unref(message);
                break;
            }
            gst_message_unref(message);
            gint n_video = 0, n_audio = 0;
            g_object_get(playbin, "n-video", &n_video, "n-audio", &n_audio, NULL);
            if (n_video < 1 || n_audio < 1) {
                fprintf(stderr, "gst-check: decoded %d video and %d audio streams\n",
                        n_video, n_audio);
                break;
            }
            result = 0;
            break;
        }
    }
    gst_element_set_state(playbin, GST_STATE_NULL);
    gst_object_unref(bus);
    gst_object_unref(playbin);
    return result;
}

int main(int argc, char **argv)
{
    gst_init(&argc, &argv);
    printf("GStreamer %s\n", gst_version_string());

    int missing = 0;
    for (size_t i = 0; i < G_N_ELEMENTS(elements); ++i) {
        GstElementFactory *factory = gst_element_factory_find(elements[i]);
        if (!factory) {
            printf("missing: %s\n", elements[i]);
            ++missing;
            continue;
        }
        GstElement *element = gst_element_factory_create(factory, NULL);
        if (!element) {
            printf("missing: %s (its plugin does not load)\n", elements[i]);
            ++missing;
            gst_object_unref(factory);
            continue;
        }
        gst_object_unref(gst_object_ref_sink(element));
        GstPlugin *plugin = gst_plugin_feature_get_plugin(GST_PLUGIN_FEATURE(factory));
        printf("element %s: %s\n", elements[i],
               plugin && gst_plugin_get_filename(plugin) ? gst_plugin_get_filename(plugin) : "(built in)");
        if (plugin)
            gst_object_unref(plugin);
        gst_object_unref(factory);
    }
    if (missing)
        return 1;

    if (argc > 1) {
        int rc = decode(argv[1]);
        if (rc != 0)
            return rc;
        printf("decoded %s\n", argv[1]);
    }
    printf("gst-check: ok\n");
    return 0;
}
