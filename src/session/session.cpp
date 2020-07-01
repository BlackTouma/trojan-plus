/*
 * This file is part of the Trojan Plus project.
 * Trojan is an unidentifiable mechanism that helps you bypass GFW.
 * Trojan Plus is derived from original trojan project and writing
 * for more experimental features.
 * Copyright (C) 2017-2020  The Trojan Authors.
 * Copyright (C) 2020 The Trojan Plus Group Authors.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "session.h"
#include "core/service.h"

using namespace std;
size_t Session::s_total_session_count = 0;

Session::Session(Service* _service, const Config& _config)
    : service(_service),
      udp_gc_timer(_service->get_io_context()),
      pipeline_com(_config),
      is_udp_forward(false),
      config(_config) {
    s_total_session_count++;
}

Session::~Session() {
    s_total_session_count--;
    _log_with_date_time_ALL(
      "[mem] checking memory leak, current all session count is " + to_string(s_total_session_count));
};

int Session::get_udp_timer_timeout_val() const { return get_config().get_udp_timeout(); }

void Session::udp_timer_async_wait(int timeout /*=-1*/) {
    if (!is_udp_forward_session()) {
        return;
    }

    bool check = timeout == -1;

    if (timeout == -1) {
        timeout = get_udp_timer_timeout_val();
    }

    if (udp_gc_timer_checker != 0 && check) {
        auto curr = time(nullptr);
        if (curr - udp_gc_timer_checker < timeout) {
            udp_gc_timer_checker = curr;
            return;
        }
    } else {
        udp_gc_timer_checker = time(nullptr);
    }

    boost::system::error_code ec;
    udp_gc_timer.cancel(ec);
    if (ec) {
        output_debug_info_ec(ec);
        destroy();
        return;
    }

    udp_gc_timer.expires_after(chrono::seconds(timeout));
    auto self = shared_from_this();
    udp_gc_timer.async_wait([this, self, timeout](const boost::system::error_code error) {
        if (!error) {
            auto curr = time(nullptr);
            if (curr - udp_gc_timer_checker < timeout) {
                auto diff            = int(timeout - (curr - udp_gc_timer_checker));
                udp_gc_timer_checker = 0;
                udp_timer_async_wait(diff);
                return;
            }

            _log_with_date_time("session_id: " + to_string(get_session_id()) + " UDP session timeout");
            destroy();
        }
    });
}

void Session::udp_timer_cancel() {
    if (udp_gc_timer_checker == 0) {
        return;
    }

    boost::system::error_code ec;
    udp_gc_timer.cancel(ec);
    if (ec) {
        output_debug_info_ec(ec);
    }
}