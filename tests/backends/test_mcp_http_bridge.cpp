#include "hikari/backends/ai_mcp_bridge.h"

#include <QCoreApplication>

#include <cstring>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--channel-url") == 0)
            hikari::backends::ai::runMcpHttpBridge(QString::fromLocal8Bit(argv[i + 1]),
                                                   qgetenv(hikari::backends::ai::kMcpTokenVariable));
        if (std::strcmp(argv[i], "--channel") == 0)
            hikari::backends::ai::runMcpBridge(hikari::backends::ai::McpEndpoint{
                QString::fromLocal8Bit(argv[i + 1]), qgetenv(hikari::backends::ai::kMcpTokenVariable), 0});
    }
    return 2;
}
