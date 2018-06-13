#include "net.hpp"
#include <gio/gio.h>
#include <glib-unix.h>
#include <iostream>
#include <signal.h>

struct network_provider_backend
{
    /* setup backend AND initially populate the connection info, then set updated = true */
    virtual bool create(connection_info *store) = 0;
    /* called by the thread starter, see network_widget source code */
    virtual void thread_loop() = 0;

    virtual ~network_provider_backend() = 0;
};

network_provider_backend::~network_provider_backend()
{
}

static void
on_wifi_properties_changed (GDBusProxy          *proxy,
                            GVariant            *changed_properties,
                            const gchar* const  *invalidated_properties,
                            gpointer             user_data)
{
    connection_info *info = (connection_info*) user_data;
    if (g_variant_n_children(changed_properties) > 0)
    {
        GVariantIter *iter;
        g_variant_get(changed_properties, "a{sv}", &iter);

        const gchar *key;
        GVariant *value;
        while (g_variant_iter_loop(iter, "{&sv}", &key, &value))
        {
            if (std::string(key) == "Strength")
            {
                info->mutex.lock();
                info->strength = g_variant_get_byte(value);
                for (auto& p : info->updated)
                    p.second = true;
                info->mutex.unlock();
            }
        }

        g_variant_iter_free(iter);
    }
}

using updater_callback = std::function<void()>;

static void
on_nm_properties_changed (GDBusProxy          *proxy,
                            GVariant            *changed_properties,
                            const gchar* const  *invalidated_properties,
                            gpointer             user_data)
{
    updater_callback *callback = (updater_callback*) user_data;
    if (g_variant_n_children(changed_properties) > 0)
    {
        GVariantIter *iter;
        g_variant_get(changed_properties, "a{sv}", &iter);

        const gchar *key;
        GVariant *value;
        while (g_variant_iter_loop(iter, "{&sv}", &key, &value))
        {
            if (std::string(key) == "PrimaryConnection")
                (*callback)();
        }

        g_variant_iter_free(iter);
    }
}

static gboolean handle_exit_signal(gpointer data)
{
    g_main_loop_quit((GMainLoop*) data);
    return G_SOURCE_REMOVE;
}

struct network_manager_provider : public network_provider_backend
{
    connection_info *info;
    GDBusConnection *dbus_connection;
    GDBusProxy *nm_proxy;

#define NM_DBUS_NAME "org.freedesktop.NetworkManager"

    bool setup_dbus_connection()
    {
        GError *error = NULL;
        dbus_connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);

        if (!dbus_connection)
        {
            std::cerr << "Failed to connect to system bus: "
                      << error->message << std::endl;
            return false;
        }

        nm_proxy = g_dbus_proxy_new_sync(dbus_connection,
                                         G_DBUS_PROXY_FLAGS_NONE, NULL,
                                         NM_DBUS_NAME,
                                         "/org/freedesktop/NetworkManager",
                                         "org.freedesktop.NetworkManager",
                                         NULL, &error);

        return nm_proxy != NULL && !error;
    }

    GDBusProxy* current_specific_proxy = NULL;

    void load_wifi_data(const gchar *ap)
    {
        GError *error = NULL;
        current_specific_proxy =
            g_dbus_proxy_new_sync(dbus_connection, G_DBUS_PROXY_FLAGS_NONE, NULL,
                                  NM_DBUS_NAME,
                                  ap,
                                  "org.freedesktop.NetworkManager.AccessPoint",
                                  NULL, &error);

        if (!current_specific_proxy)
        {
            std::cerr << "Failed to obtain AP info: " << error->message << std::endl;
            return;
        }

        GVariant *gv = g_dbus_proxy_get_cached_property(current_specific_proxy, "Strength");

        info->mutex.lock();
        info->strength = g_variant_get_byte(gv);

        for (auto& p : info->updated)
            p.second = true;
        info->mutex.unlock();

        g_variant_unref(gv);

        g_signal_connect(current_specific_proxy, "g-properties-changed",
                         G_CALLBACK(on_wifi_properties_changed), info);
    }

    void load_bluetooth_data(const gchar *dev)
    {
        info->mutex.lock();
        /* TODO: implement */
    }

    void load_ethernet_data(const gchar *dev)
    {
        info->mutex.lock();

        info->icon = "none";
        info->strength = 100;
        info->name = "Ethernet";

        for (auto& p : info->updated)
            p.second = true;
        info->mutex.unlock();
    }

    void active_connection_updated()
    {
        GVariant *gv = g_dbus_proxy_get_cached_property(
                nm_proxy, "PrimaryConnection");

        if (!gv)
        {
            std::cerr << "Couldn't get information about the primary internet connection" << std::endl;
            return;
        }

        gsize n;
        const gchar *active_connection = g_variant_get_string(gv, &n);

        /* no active connection */
        if (std::string(active_connection) == "/")
        {
            info->mutex.lock();
            info->name = "No network";
            for (auto& p : info->updated)
                p.second = true;
            info->strength = 0;
            info->mutex.unlock();

            return;
        }

        GError *error = NULL;
        GDBusProxy *aconn_proxy =
            g_dbus_proxy_new_sync(dbus_connection,
                                  G_DBUS_PROXY_FLAGS_NONE, NULL,
                                  NM_DBUS_NAME,
                                  active_connection,
                                  "org.freedesktop.NetworkManager.Connection.Active",
                                  NULL, &error);

        if (!aconn_proxy)
        {
            std::cerr << "Failed to get active connection: " << error->message << std::endl;
            return;
        }

        gv = g_dbus_proxy_get_cached_property(aconn_proxy, "Type");
        std::string type = g_variant_get_string(gv, &n);

        g_variant_unref(gv);
        gv = g_dbus_proxy_get_cached_property(aconn_proxy, "Id");

        info->mutex.lock();
        info->name = g_variant_get_string(gv, &n);
        info->mutex.unlock();

        g_variant_unref(gv);
        gv = g_dbus_proxy_get_cached_property(aconn_proxy, "SpecificObject");
        const gchar *object = g_variant_get_string(gv, &n);

        if (current_specific_proxy)
            g_object_unref(current_specific_proxy);

        if (type == "bluetooth")
        {
            load_bluetooth_data(object);
        } else if (type.find("ethernet") != std::string::npos)
        {
            load_ethernet_data(object);
        } else if (type.find("wireless") != std::string::npos)
        {
            load_wifi_data(object);
        }

        g_variant_unref(gv);
        g_object_unref(aconn_proxy);
    }

    void load_initial_connection_info()
    {
        /* don't have to lock mutex, as we are still in the main thread */
        for (auto& p : info->updated)
            p.second = true;
        info->icon = info->name = "none";
        info->strength = 0;

        active_connection_updated();
    }

    bool create(connection_info *info)
    {
        this->info = info;

        if (!setup_dbus_connection())
            return false;

        load_initial_connection_info();
        return true;
    }

    void thread_loop()
    {
        updater_callback callback =
            std::bind(std::mem_fn(&network_manager_provider::active_connection_updated), this);

        g_signal_connect(nm_proxy, "g-properties-changed",
                         G_CALLBACK(on_nm_properties_changed), &callback);

        GMainLoop *loop = g_main_loop_new(NULL, false);
        g_unix_signal_add(SIGUSR1, handle_exit_signal, loop);

        g_main_loop_run(loop);

        g_main_loop_unref(loop);
        g_object_unref(nm_proxy);
    }

    ~network_manager_provider()
    {
    }
};

std::thread network_widget::updater_thread;
connection_info *network_widget::connection;
network_provider_backend *network_widget::backend = nullptr;

void network_widget::create()
{
    if (!backend)
    {
        backend = new network_manager_provider();
        connection = new connection_info;
        if (!backend || !connection || !backend->create(connection))
        {
            std::cerr << "Failed to create network backend!" << std::endl;

            delete backend;
            backend = nullptr;
            return;
        }

        updater_thread = std::thread([=] () { backend->thread_loop(); });
    }

    connection->mutex.lock();
    id = (connection->updated.empty() ? 0 : (--connection->updated.end())->first) + 1;
    connection->updated[id] = true;
    connection->mutex.unlock();

    load_default_font();
    width = 20 * font_size;
}

network_widget::~network_widget()
{
    connection->mutex.lock();
    connection->updated.erase(id);
    connection->mutex.unlock();
}

bool network_widget::update()
{
    if (!backend)
        return false;

    bool result;
    std::string text;
    connection->mutex.lock();
    result = connection->updated[id];
    text = connection->name;
    connection->mutex.unlock();

    if (result)
    {
        cairo_set_font_size(cr, font_size);
        cairo_set_font_face(cr, cairo_font_face);

        cairo_text_extents_t te;
        cairo_text_extents(cr, text.c_str(), &te);

        width = te.width + font_size;
    }

    return result;
}

inline wf_color interpolate_color(wf_color start, wf_color end, float a)
{
    wf_color r;
    r.r = start.r * a + end.r * (1. - a);
    r.g = start.g * a + end.g * (1. - a);
    r.b = start.b * a + end.b * (1. - a);
    r.a = start.a * a + end.a * (1. - a);

    return r;
}

void network_widget::repaint()
{
    if (!backend)
        return;

    constexpr wf_color color_good = {0, 1, 0, 1},
                            color_avg  = {1, 1, 0.3, 1},
                            color_bad  = {1, 0, 0, 1};

    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    std::string text;
    wf_color color;

    connection->mutex.lock();

#define STRENGTH_GOOD 40
#define STRENGTH_AVG 25

    text = connection->name;
    if (connection->strength >= STRENGTH_GOOD)
        color = interpolate_color(color_good, color_avg,
                (connection->strength - STRENGTH_GOOD) * 1.0 / (100 - STRENGTH_GOOD));
    else if (connection->strength >= STRENGTH_AVG)
        color = interpolate_color(color_avg, color_bad,
                (connection->strength - STRENGTH_AVG) * 1.0 / (STRENGTH_GOOD - STRENGTH_AVG));
    else
        color = color_bad;

    connection->updated[id] = false;
    connection->mutex.unlock();

    cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);

    cairo_move_to(cr, x + font_size * 0.5, font_size);
    cairo_show_text(cr, text.c_str());
}
