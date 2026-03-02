/// @file test_types.cpp
/// Unit tests for types.hpp — enums, validation helpers, and protocol constants.

#include <gtest/gtest.h>
#include <sero/core/types.hpp>

using namespace sero;

// ── Protocol Constants ──────────────────────────────────────────

TEST(Types_Constants, ProtocolVersion_Is0x01) {
    EXPECT_EQ(PROTOCOL_VERSION, 0x01);
}

TEST(Types_Constants, HeaderSize_Is20) {
    EXPECT_EQ(HEADER_SIZE, 20u);
}

TEST(Types_Constants, MinMessageSize_Is22) {
    EXPECT_EQ(MIN_MESSAGE_SIZE, HEADER_SIZE + CRC_SIZE);
    EXPECT_EQ(MIN_MESSAGE_SIZE, 22u);
}

TEST(Types_Constants, SdServiceId_Is0xFFFF) {
    EXPECT_EQ(SD_SERVICE_ID, 0xFFFF);
}

// ── is_valid_message_type ───────────────────────────────────────

TEST(Types_IsValidMessageType, ValidValues_ReturnTrue) {
    EXPECT_TRUE(is_valid_message_type(0x00)); // REQUEST
    EXPECT_TRUE(is_valid_message_type(0x01)); // REQUEST_NO_RETURN
    EXPECT_TRUE(is_valid_message_type(0x02)); // RESPONSE
    EXPECT_TRUE(is_valid_message_type(0x03)); // NOTIFICATION
    EXPECT_TRUE(is_valid_message_type(0x80)); // ERROR
}

TEST(Types_IsValidMessageType, InvalidValues_ReturnFalse) {
    EXPECT_FALSE(is_valid_message_type(0x04));
    EXPECT_FALSE(is_valid_message_type(0x7F));
    EXPECT_FALSE(is_valid_message_type(0x81));
    EXPECT_FALSE(is_valid_message_type(0xFF));
    EXPECT_FALSE(is_valid_message_type(0x10));
}

// ── is_event_id / is_method_id ─────────────────────────────────

TEST(Types_IsEventId, Bit15Set_ReturnsTrue) {
    EXPECT_TRUE(is_event_id(0x8000));
    EXPECT_TRUE(is_event_id(0x8001));
    EXPECT_TRUE(is_event_id(0xFFFF));
}

TEST(Types_IsEventId, Bit15Clear_ReturnsFalse) {
    EXPECT_FALSE(is_event_id(0x0000));
    EXPECT_FALSE(is_event_id(0x0001));
    EXPECT_FALSE(is_event_id(0x7FFF));
}

TEST(Types_IsMethodId, Bit15Clear_ReturnsTrue) {
    EXPECT_TRUE(is_method_id(0x0000));
    EXPECT_TRUE(is_method_id(0x0001));
    EXPECT_TRUE(is_method_id(0x7FFF));
}

TEST(Types_IsMethodId, Bit15Set_ReturnsFalse) {
    EXPECT_FALSE(is_method_id(0x8000));
    EXPECT_FALSE(is_method_id(0xFFFF));
}

TEST(Types_IsEventId, BoundaryAt0x7FFF_ReturnsFalse) {
    EXPECT_FALSE(is_event_id(0x7FFF));
}

TEST(Types_IsEventId, BoundaryAt0x8000_ReturnsTrue) {
    EXPECT_TRUE(is_event_id(0x8000));
}

// ── type_id_consistent ─────────────────────────────────────────

TEST(Types_TypeIdConsistent, Request_WithMethodId_ReturnsTrue) {
    EXPECT_TRUE(type_id_consistent(MessageType::REQUEST, 0x0001));
    EXPECT_TRUE(type_id_consistent(MessageType::REQUEST, 0x7FFF));
}

TEST(Types_TypeIdConsistent, Request_WithEventId_ReturnsFalse) {
    EXPECT_FALSE(type_id_consistent(MessageType::REQUEST, 0x8000));
    EXPECT_FALSE(type_id_consistent(MessageType::REQUEST, 0xFFFF));
}

TEST(Types_TypeIdConsistent, RequestNoReturn_WithMethodId_ReturnsTrue) {
    EXPECT_TRUE(type_id_consistent(MessageType::REQUEST_NO_RETURN, 0x0001));
}

TEST(Types_TypeIdConsistent, RequestNoReturn_WithEventId_ReturnsFalse) {
    EXPECT_FALSE(type_id_consistent(MessageType::REQUEST_NO_RETURN, 0x8001));
}

TEST(Types_TypeIdConsistent, Response_WithMethodId_ReturnsTrue) {
    EXPECT_TRUE(type_id_consistent(MessageType::RESPONSE, 0x0001));
}

TEST(Types_TypeIdConsistent, Response_WithEventId_ReturnsFalse) {
    EXPECT_FALSE(type_id_consistent(MessageType::RESPONSE, 0x8001));
}

TEST(Types_TypeIdConsistent, Error_WithMethodId_ReturnsTrue) {
    EXPECT_TRUE(type_id_consistent(MessageType::ERROR, 0x0001));
}

TEST(Types_TypeIdConsistent, Error_WithEventId_ReturnsFalse) {
    EXPECT_FALSE(type_id_consistent(MessageType::ERROR, 0x8001));
}

TEST(Types_TypeIdConsistent, Notification_WithEventId_ReturnsTrue) {
    EXPECT_TRUE(type_id_consistent(MessageType::NOTIFICATION, 0x8001));
    EXPECT_TRUE(type_id_consistent(MessageType::NOTIFICATION, 0xFFFF));
}

TEST(Types_TypeIdConsistent, Notification_WithMethodId_ReturnsFalse) {
    EXPECT_FALSE(type_id_consistent(MessageType::NOTIFICATION, 0x0001));
    EXPECT_FALSE(type_id_consistent(MessageType::NOTIFICATION, 0x7FFF));
}
