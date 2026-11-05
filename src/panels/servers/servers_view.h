#pragma once

#include "data/server/server_data.h"

#include "widget/canvas.h"
#include "widget/geom.h"
#include "widget/module.h"

namespace servers_view {

int BodyHeight(const server::ServerData& data, bool expanded);
void DrawBody(const server::ServerData& data, wg::Canvas& canvas, const wg::Rect& body,
              const wg::Theme& theme, bool expanded);
wg::StatusChip Status(const server::ServerData& data, const wg::Theme& theme);

} // namespace servers_view
