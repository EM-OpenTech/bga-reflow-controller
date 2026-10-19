/*
 * SPDX-FileCopyrightText: 2026 EM-OpenTech
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file rest_api.cpp
 * @brief Implementation of REST API Facade and lifecycle initialization.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "web/rest_api.hpp"

namespace web {

void RestApi::init(storage::StorageManager* storage, config::MachineSettings* settings, fsm::ReflowFSM* fsm,
                   WifiManager* wifi, app::SystemContext* context) {
    auto& ctx = ApiContext::instance();
    ctx.storage = storage;
    ctx.settings = settings;
    ctx.fsm = fsm;
    ctx.wifi = wifi;
    ctx.context = context;
}

void RestApi::setTaskHandles(const TaskHandles& handles) {
    ApiContext::instance().taskHandles = handles;
}

void RestApi::setPostReport(const PostReportInfo& postInfo) {
    ApiContext::instance().postReport = postInfo;
}

} // namespace web
