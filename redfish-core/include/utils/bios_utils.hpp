// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once

#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "logging.hpp"

#include <sdbusplus/message/native_types.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>

namespace redfish
{
namespace bios_utils
{
static constexpr std::string_view biosConfigManagerPath =
    "/xyz/openbmc_project/bios_config/manager";
static constexpr std::string_view biosConfigManagerInterface =
    "xyz.openbmc_project.BIOSConfig.Manager";

template <typename Type>
inline void extractValue(nlohmann::json& attributes, const std::string& name,
                         const dbus::utility::DbusVariantType& value)
{
    const Type* tValue = std::get_if<Type>(&value);
    if (tValue != nullptr)
    {
        attributes[name] = *tValue;
        return;
    }
    attributes[name] = Type{};
}

template <>
inline void extractValue<bool>(nlohmann::json& attributes,
                               const std::string& name,
                               const dbus::utility::DbusVariantType& value)
{
    const bool* boolValue = std::get_if<bool>(&value);
    if (boolValue != nullptr)
    {
        attributes[name] = *boolValue;
        return;
    }
    const int64_t* tValue = std::get_if<int64_t>(&value);
    if (tValue != nullptr)
    {
        attributes[name] = (*tValue != 0);
        return;
    }
    attributes[name] = false;
}

using HandlerType = std::function<void(nlohmann::json&, const std::string&,
                                       const dbus::utility::DbusVariantType&)>;

inline void addAttribute(nlohmann::json& attributes, const std::string& name,
                         const dbus::utility::DbusVariantType& type,
                         const dbus::utility::DbusVariantType& value)
{
    static const std::unordered_map<std::string, HandlerType> typeMap = {
        {"xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Enumeration",
         extractValue<std::string>},
        {"xyz.openbmc_project.BIOSConfig.Manager.AttributeType.String",
         extractValue<std::string>},
        {"xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Password",
         extractValue<std::string>},
        {"xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Integer",
         extractValue<int64_t>},
        {"xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Boolean",
         extractValue<bool>}};

    const std::string* typeStr = std::get_if<std::string>(&type);
    if (typeStr != nullptr)
    {
        auto it = typeMap.find(*typeStr);
        if (it != typeMap.end())
        {
            it->second(attributes, name, value);
        }
    }
}

template <typename T>
inline void getBIOSManagerProperty(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& property, const std::string& objectPath,
    std::function<void(const T&)> handler)
{
    sdbusplus::asio::getProperty<T>(
        *crow::connections::systemBus, objectPath,
        std::string(biosConfigManagerPath),
        std::string(biosConfigManagerInterface), property,
        [asyncResp, property, handler{std::move(handler)}](
            const boost::system::error_code& ec, const T& value) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBus response error for {}: {}", property,
                                 ec);
                messages::internalError(asyncResp->res);
                return;
            }
            handler(value);
        });
}

template <typename CallbackFunc>
inline void getBIOSManagerObject(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    CallbackFunc&& callback)
{
    dbus::utility::getDbusObject(
        std::string(biosConfigManagerPath),
        std::array<std::string_view, 1>{biosConfigManagerInterface},
        [asyncResp, callback = std::forward<CallbackFunc>(callback)](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetObject& object) {
            if (ec || object.empty())
            {
                BMCWEB_LOG_ERROR("Error finding BIOS Manager object {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }
            if (object.size() > 1)
            {
                BMCWEB_LOG_ERROR("More than one BIOS Manager object found");
                messages::internalError(asyncResp->res);
                return;
            }
            callback(object.begin()->first);
        });
}

// Matches the D-Bus interface exactly:
//   property type: a{s(se)}  where the variant is restricted to
//   std::variant<int64_t, std::string> and the type tag is the
//   AttributeType sdbusplus enum (serialised as its dotted string form).
//
// The enum and its sdbusplus serialization specializations replicate
// xyz/openbmc_project/BIOSConfig/Manager/common.hpp, which is not available
// in all bmcweb build sysroots.
enum class AttributeType
{
    Enumeration,
    String,
    Password,
    Integer,
    Boolean,
};

inline AttributeType attributeTypeFromString(const std::string& s)
{
    if (s == "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Enumeration")
        return AttributeType::Enumeration;
    if (s == "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Integer")
        return AttributeType::Integer;
    if (s == "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Boolean")
        return AttributeType::Boolean;
    if (s == "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Password")
        return AttributeType::Password;
    // Default to String for unknown types
    return AttributeType::String;
}

inline std::string attributeTypeToString(AttributeType e)
{
    switch (e)
    {
        case AttributeType::Enumeration:
            return "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Enumeration";
        case AttributeType::String:
            return "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.String";
        case AttributeType::Password:
            return "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Password";
        case AttributeType::Integer:
            return "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Integer";
        case AttributeType::Boolean:
            return "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Boolean";
    }
    return "";
}

using PendingAttributeValue =
    std::tuple<AttributeType, std::variant<int64_t, std::string>>;
enum class PendingAttributeValueIndex
{
    Type = 0,
    Value
};

using PendingAttributes =
    std::map<std::string, PendingAttributeValue>;

inline std::string pendingAttributeValueToStringCrystalDebug(
    const std::variant<int64_t, std::string>& value)
{
    return std::visit(
        [](const auto& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::same_as<T, std::string>)
            {
                return v;
            }
            else
            {
                return std::to_string(v);
            }
        },
        value);
}

inline void setBIOSManagerProperty(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& propertyName, const PendingAttributes& propertyValue,
    const std::string& objectPath)
{
    BMCWEB_LOG_DEBUG(
        "crystal debug: setting BIOS manager property '{}' on object '{}' interface '{}' entries={}",
        propertyName, objectPath, biosConfigManagerInterface,
        propertyValue.size());
    for (const auto& [name, pendingValue] : propertyValue)
    {
        BMCWEB_LOG_DEBUG(
            "crystal debug: pending attribute name='{}' type='{}' value='{}'",
            name, attributeTypeToString(std::get<0>(pendingValue)),
            pendingAttributeValueToStringCrystalDebug(std::get<1>(pendingValue)));
    }

    sdbusplus::asio::setProperty(
        *crow::connections::systemBus, objectPath,
        std::string(biosConfigManagerPath),
        std::string(biosConfigManagerInterface), propertyName, propertyValue,
        [asyncResp, propertyName,
         objectPath](const boost::system::error_code& ec) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "crystal debug: DBus response error for setting '{}' on object '{}': value={} category='{}' message='{}'",
                    propertyName, objectPath, ec.value(), ec.category().name(),
                    ec.message());
                messages::internalError(asyncResp->res);
                return;
            }
            BMCWEB_LOG_DEBUG(
                "crystal debug: successfully set BIOS manager property '{}' on object '{}'",
                propertyName, objectPath);
        });
}

} // namespace bios_utils
} // namespace redfish

// Teach sdbusplus how to serialize/deserialize our local AttributeType enum
// over D-Bus (it is treated as SD_BUS_TYPE_STRING on the wire).
namespace sdbusplus::message::details
{

template <>
struct convert_to_string<redfish::bios_utils::AttributeType>
{
    static std::string op(redfish::bios_utils::AttributeType e)
    {
        return redfish::bios_utils::attributeTypeToString(e);
    }
};

template <>
struct convert_from_string<redfish::bios_utils::AttributeType>
{
    static std::optional<redfish::bios_utils::AttributeType>
        op(const std::string& s) noexcept
    {
        using AT = redfish::bios_utils::AttributeType;
        if (s == "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Enumeration")
            return AT::Enumeration;
        if (s == "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.String")
            return AT::String;
        if (s == "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Password")
            return AT::Password;
        if (s == "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Integer")
            return AT::Integer;
        if (s == "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Boolean")
            return AT::Boolean;
        return std::nullopt;
    }
};

} // namespace sdbusplus::message::details
