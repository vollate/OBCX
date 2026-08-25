#ifndef OBCX_INCLUDE_CORE_MESSAGE_EVENT_INGRESS_HPP_
#define OBCX_INCLUDE_CORE_MESSAGE_EVENT_INGRESS_HPP_

#include "common/message_type.hpp"
#include "core/actor.hpp"

#include <string>

namespace obcx::core {

auto raw_message_envelope_from_event(const std::string &source_platform,
                                     const std::string &source_bot,
                                     const common::MessageEvent &event)
    -> MessageEnvelope;

auto raw_notice_envelope_from_event(const std::string &source_platform,
                                    const std::string &source_bot,
                                    const common::NoticeEvent &event)
    -> MessageEnvelope;

} // namespace obcx::core

#endif // OBCX_INCLUDE_CORE_MESSAGE_EVENT_INGRESS_HPP_
