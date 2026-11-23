#include "hikari/backends/plugin_link.h"

namespace hikari::backends {

HelperPluginLink::HelperPluginLink(std::unique_ptr<helper::HelperHost> host, QObject *parent)
    : PluginLink(parent), m_host(std::move(host))
{
    connect(m_host.get(), &helper::HelperHost::ready, this, &PluginLink::started);
    connect(m_host.get(), &helper::HelperHost::refused, this,
            [this] { emit refused(QStringLiteral("hikari-plugin-helper speaks another protocol version")); });
    connect(m_host.get(), &helper::HelperHost::lost, this, &PluginLink::lost);
}

HelperPluginLink::~HelperPluginLink()
{
    disconnect(m_host.get(), nullptr, this, nullptr);
}

}
