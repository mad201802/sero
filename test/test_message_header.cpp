/// @file test_message_header.cpp
/// Unit tests for message_header.hpp — serialize/deserialize, validation helpers.

#include <gtest/gtest.h>
#include <sero/core/message_header.hpp>

#include <cstring>

using namespace sero;

// ── read_u16 / write_u16 / read_u32 / write_u32 ────────────────

TEST(MessageHeader_ReadWriteU16, RoundTrip) {
    uint8_t buf[2];
    MessageHeader::write_u16(buf, 0xABCD);
    EXPECT_EQ(MessageHeader::read_u16(buf), 0xABCD);
}

TEST(MessageHeader_ReadWriteU16, BigEndianByteOrder) {
    uint8_t buf[2];
    MessageHeader::write_u16(buf, 0x1234);
    EXPECT_EQ(buf[0], 0x12);
    EXPECT_EQ(buf[1], 0x34);
}

TEST(MessageHeader_ReadWriteU16, BoundaryValues) {
    uint8_t buf[2];
    MessageHeader::write_u16(buf, 0x0000);
    EXPECT_EQ(MessageHeader::read_u16(buf), 0x0000);

    MessageHeader::write_u16(buf, 0xFFFF);
    EXPECT_EQ(MessageHeader::read_u16(buf), 0xFFFF);
}

TEST(MessageHeader_ReadWriteU32, RoundTrip) {
    uint8_t buf[4];
    MessageHeader::write_u32(buf, 0xDEADBEEF);
    EXPECT_EQ(MessageHeader::read_u32(buf), 0xDEADBEEF);
}

TEST(MessageHeader_ReadWriteU32, BigEndianByteOrder) {
    uint8_t buf[4];
    MessageHeader::write_u32(buf, 0x12345678);
    EXPECT_EQ(buf[0], 0x12);
    EXPECT_EQ(buf[1], 0x34);
    EXPECT_EQ(buf[2], 0x56);
    EXPECT_EQ(buf[3], 0x78);
}

TEST(MessageHeader_ReadWriteU32, BoundaryValues) {
    uint8_t buf[4];
    MessageHeader::write_u32(buf, 0x00000000);
    EXPECT_EQ(MessageHeader::read_u32(buf), 0x00000000u);

    MessageHeader::write_u32(buf, 0xFFFFFFFF);
    EXPECT_EQ(MessageHeader::read_u32(buf), 0xFFFFFFFF);
}

// ── SerializeDeserialize round-trip ─────────────────────────────

TEST(MessageHeader_SerializeDeserialize, RoundTrip_AllFields) {
    MessageHeader hdr;
    hdr.version          = 0x01;
    hdr.message_type     = 0x00; // REQUEST
    hdr.return_code      = 0x00;
    hdr.flags            = 0x01; // AUTH
    hdr.service_id       = 0x1234;
    hdr.method_event_id  = 0x0001;
    hdr.client_id        = 0xABCD;
    hdr.sequence_counter = 0x42;
    hdr.reserved         = 0x00;
    hdr.request_id       = 0xDEADBEEF;
    hdr.payload_length   = 0x00000100;

    uint8_t buf[MessageHeader::SIZE];
    hdr.serialize(buf);

    MessageHeader hdr2 = MessageHeader::deserialize(buf);

    EXPECT_EQ(hdr2.version, hdr.version);
    EXPECT_EQ(hdr2.message_type, hdr.message_type);
    EXPECT_EQ(hdr2.return_code, hdr.return_code);
    EXPECT_EQ(hdr2.flags, hdr.flags);
    EXPECT_EQ(hdr2.service_id, hdr.service_id);
    EXPECT_EQ(hdr2.method_event_id, hdr.method_event_id);
    EXPECT_EQ(hdr2.client_id, hdr.client_id);
    EXPECT_EQ(hdr2.sequence_counter, hdr.sequence_counter);
    EXPECT_EQ(hdr2.reserved, hdr.reserved);
    EXPECT_EQ(hdr2.request_id, hdr.request_id);
    EXPECT_EQ(hdr2.payload_length, hdr.payload_length);
}

TEST(MessageHeader_SerializeDeserialize, MaxValues) {
    MessageHeader hdr;
    hdr.version          = 0xFF;
    hdr.message_type     = 0xFF;
    hdr.return_code      = 0xFF;
    hdr.flags            = 0xFF;
    hdr.service_id       = 0xFFFF;
    hdr.method_event_id  = 0xFFFF;
    hdr.client_id        = 0xFFFF;
    hdr.sequence_counter = 0xFF;
    hdr.reserved         = 0xFF;
    hdr.request_id       = 0xFFFFFFFF;
    hdr.payload_length   = 0xFFFFFFFF;

    uint8_t buf[MessageHeader::SIZE];
    hdr.serialize(buf);
    MessageHeader hdr2 = MessageHeader::deserialize(buf);

    EXPECT_EQ(hdr2.version, 0xFF);
    EXPECT_EQ(hdr2.service_id, 0xFFFF);
    EXPECT_EQ(hdr2.request_id, 0xFFFFFFFF);
    EXPECT_EQ(hdr2.payload_length, 0xFFFFFFFF);
}

// ── Wire format byte positions ──────────────────────────────────

TEST(MessageHeader_Serialize, BytePositions) {
    MessageHeader hdr;
    hdr.version          = 0xAA;
    hdr.message_type     = 0xBB;
    hdr.return_code      = 0xCC;
    hdr.flags            = 0xDD;
    hdr.service_id       = 0x1122;
    hdr.method_event_id  = 0x3344;
    hdr.client_id        = 0x5566;
    hdr.sequence_counter = 0x77;
    hdr.reserved         = 0x88;
    hdr.request_id       = 0x99AABBCC;
    hdr.payload_length   = 0xDDEEFF00;

    uint8_t buf[MessageHeader::SIZE];
    hdr.serialize(buf);

    EXPECT_EQ(buf[0],  0xAA); // version
    EXPECT_EQ(buf[1],  0xBB); // message_type
    EXPECT_EQ(buf[2],  0xCC); // return_code
    EXPECT_EQ(buf[3],  0xDD); // flags
    EXPECT_EQ(buf[4],  0x11); // service_id high
    EXPECT_EQ(buf[5],  0x22); // service_id low
    EXPECT_EQ(buf[6],  0x33); // method_event_id high
    EXPECT_EQ(buf[7],  0x44); // method_event_id low
    EXPECT_EQ(buf[8],  0x55); // client_id high
    EXPECT_EQ(buf[9],  0x66); // client_id low
    EXPECT_EQ(buf[10], 0x77); // sequence_counter
    EXPECT_EQ(buf[11], 0x88); // reserved
    EXPECT_EQ(buf[12], 0x99); // request_id byte 0
    EXPECT_EQ(buf[13], 0xAA); // request_id byte 1
    EXPECT_EQ(buf[14], 0xBB); // request_id byte 2
    EXPECT_EQ(buf[15], 0xCC); // request_id byte 3
    EXPECT_EQ(buf[16], 0xDD); // payload_length byte 0
    EXPECT_EQ(buf[17], 0xEE); // payload_length byte 1
    EXPECT_EQ(buf[18], 0xFF); // payload_length byte 2
    EXPECT_EQ(buf[19], 0x00); // payload_length byte 3
}

// ── validate_type_id_consistency ────────────────────────────────

TEST(MessageHeader_ValidateTypeIdConsistency, RequestWithMethodId_ReturnsTrue) {
    MessageHeader hdr;
    hdr.message_type = static_cast<uint8_t>(MessageType::REQUEST);
    hdr.method_event_id = 0x0001; // method ID (bit 15 = 0)
    EXPECT_TRUE(hdr.validate_type_id_consistency());
}

TEST(MessageHeader_ValidateTypeIdConsistency, RequestWithEventId_ReturnsFalse) {
    MessageHeader hdr;
    hdr.message_type = static_cast<uint8_t>(MessageType::REQUEST);
    hdr.method_event_id = 0x8001; // event ID (bit 15 = 1)
    EXPECT_FALSE(hdr.validate_type_id_consistency());
}

TEST(MessageHeader_ValidateTypeIdConsistency, NotificationWithEventId_ReturnsTrue) {
    MessageHeader hdr;
    hdr.message_type = static_cast<uint8_t>(MessageType::NOTIFICATION);
    hdr.method_event_id = 0x8001;
    EXPECT_TRUE(hdr.validate_type_id_consistency());
}

TEST(MessageHeader_ValidateTypeIdConsistency, NotificationWithMethodId_ReturnsFalse) {
    MessageHeader hdr;
    hdr.message_type = static_cast<uint8_t>(MessageType::NOTIFICATION);
    hdr.method_event_id = 0x0001;
    EXPECT_FALSE(hdr.validate_type_id_consistency());
}

// ── validate_client_id ──────────────────────────────────────────

TEST(MessageHeader_ValidateClientId, RequestWithZero_ReturnsFalse) {
    MessageHeader hdr;
    hdr.message_type = static_cast<uint8_t>(MessageType::REQUEST);
    hdr.client_id = 0x0000;
    EXPECT_FALSE(hdr.validate_client_id());
}

TEST(MessageHeader_ValidateClientId, RequestNoReturnWithZero_ReturnsFalse) {
    MessageHeader hdr;
    hdr.message_type = static_cast<uint8_t>(MessageType::REQUEST_NO_RETURN);
    hdr.client_id = 0x0000;
    EXPECT_FALSE(hdr.validate_client_id());
}

TEST(MessageHeader_ValidateClientId, RequestWithNonZero_ReturnsTrue) {
    MessageHeader hdr;
    hdr.message_type = static_cast<uint8_t>(MessageType::REQUEST);
    hdr.client_id = 0x0001;
    EXPECT_TRUE(hdr.validate_client_id());
}

TEST(MessageHeader_ValidateClientId, ResponseWithZero_ReturnsTrue) {
    MessageHeader hdr;
    hdr.message_type = static_cast<uint8_t>(MessageType::RESPONSE);
    hdr.client_id = 0x0000;
    EXPECT_TRUE(hdr.validate_client_id());
}

TEST(MessageHeader_ValidateClientId, NotificationWithZero_ReturnsTrue) {
    MessageHeader hdr;
    hdr.message_type = static_cast<uint8_t>(MessageType::NOTIFICATION);
    hdr.client_id = 0x0000;
    EXPECT_TRUE(hdr.validate_client_id());
}

TEST(MessageHeader_ValidateClientId, ErrorWithZero_ReturnsTrue) {
    MessageHeader hdr;
    hdr.message_type = static_cast<uint8_t>(MessageType::ERROR);
    hdr.client_id = 0x0000;
    EXPECT_TRUE(hdr.validate_client_id());
}

// ── has_auth ────────────────────────────────────────────────────

TEST(MessageHeader_HasAuth, FlagSet_ReturnsTrue) {
    MessageHeader hdr;
    hdr.flags = FLAG_AUTH;
    EXPECT_TRUE(hdr.has_auth());
}

TEST(MessageHeader_HasAuth, FlagClear_ReturnsFalse) {
    MessageHeader hdr;
    hdr.flags = 0x00;
    EXPECT_FALSE(hdr.has_auth());
}

TEST(MessageHeader_HasAuth, MultipleFlagsSet_StillReturnsTrue) {
    MessageHeader hdr;
    hdr.flags = 0xFF; // all bits set, including AUTH bit 0
    EXPECT_TRUE(hdr.has_auth());
}

// ── Default construction ────────────────────────────────────────

TEST(MessageHeader_DefaultCtor, VersionIsProtocolVersion) {
    MessageHeader hdr;
    EXPECT_EQ(hdr.version, PROTOCOL_VERSION);
}
