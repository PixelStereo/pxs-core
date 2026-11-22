// Every public header compiles on its own, includes what it uses, and pulls in nothing from Qt or the platform.
#include <pxs/core/dmx/frame.h>
#include <pxs/core/feed/change_feed.h>
#include <pxs/core/midi/message.h>
#include <pxs/core/osc/codec.h>
#include <pxs/core/osc/names.h>
#include <pxs/core/tree/oscquery.h>
#include <pxs/core/tree/preset.h>
#include <pxs/core/tree/tree.h>
#include <pxs/core/value.h>
#include <pxs/core/version.h>
#include <pxs/core/websocket/frame.h>

#include "check.h"

#if defined(QT_VERSION) || defined(QT_CORE_LIB)
#error "pxs-core headers must not include Qt"
#endif

int main()
{
    CHECK(std::string(PXS_CORE_VERSION_STRING).size() > 0);
    return pxs_test::finish("public_headers");
}
