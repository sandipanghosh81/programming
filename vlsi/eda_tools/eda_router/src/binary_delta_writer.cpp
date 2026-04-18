/*
 * binary_delta_writer.cpp  —  Binary delta file encoder implementation
 * See binary_delta_writer.h for format specification and design notes.
 */

#include "eda_router/binary_delta_writer.h"
#include "eda_router/oasis_writer.h"   // for Point2D

#include <cstring>
#include <cassert>

namespace eda {

// File magic
static constexpr uint8_t MAGIC[4] = {'E', 'D', 'A', 'B'};
static constexpr uint16_t VERSION  = 0x0001;

// Header layout (little-endian):
//   [0..3]  magic
//   [4..5]  version
//   [6..9]  op_count  ← back-filled in finish()
//   [10..15] reserved (zeros)
static constexpr std::size_t HEADER_SIZE = 16;
static constexpr std::size_t OP_COUNT_OFFSET = 6;

// ─── Factory ──────────────────────────────────────────────────────────────────
std::expected<BinaryDeltaWriter, DeltaError>
BinaryDeltaWriter::open(std::string_view path) {
    std::fstream f(std::string(path),
                   std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
    if (!f.is_open()) {
        return std::unexpected(DeltaError{
            std::string("Cannot create delta file: ") + std::string(path)
        });
    }
    BinaryDeltaWriter w(std::move(f), std::string(path));

    // Write placeholder header (op_count back-filled in finish())
    uint8_t hdr[HEADER_SIZE] = {};
    std::memcpy(hdr, MAGIC, 4);
    std::memcpy(hdr + 4, &VERSION, 2);
    w.write_bytes(hdr, HEADER_SIZE);

    return w;
}

BinaryDeltaWriter::BinaryDeltaWriter(std::fstream file, std::string path)
    : file_(std::move(file))
    , path_(std::move(path))
    , header_op_count_offset_(OP_COUNT_OFFSET) {}

BinaryDeltaWriter::BinaryDeltaWriter(BinaryDeltaWriter&&) noexcept = default;
BinaryDeltaWriter& BinaryDeltaWriter::operator=(BinaryDeltaWriter&&) noexcept = default;

BinaryDeltaWriter::~BinaryDeltaWriter() {
    if (!finished_) finish();
}

// ─── Rectangle ops ────────────────────────────────────────────────────────────
void BinaryDeltaWriter::add_rect(uint16_t layer, uint16_t purpose,
                                  int32_t x, int32_t y, int32_t w, int32_t h) {
    write_op_header(OpCode::ADD_RECT, layer, purpose);
    write_i32(x); write_i32(y); write_i32(w); write_i32(h);
    ++op_count_;
}

void BinaryDeltaWriter::del_rect(uint16_t layer, uint16_t purpose,
                                  int32_t x, int32_t y, int32_t w, int32_t h) {
    write_op_header(OpCode::DEL_RECT, layer, purpose);
    write_i32(x); write_i32(y); write_i32(w); write_i32(h);
    ++op_count_;
}

// ─── Path ops ─────────────────────────────────────────────────────────────────
void BinaryDeltaWriter::add_path(uint16_t layer, uint16_t purpose,
                                  std::span<const Point2D> pts, int32_t half_w) {
    write_op_header(OpCode::ADD_PATH, layer, purpose);
    write_path_payload(pts, half_w);
    ++op_count_;
}

void BinaryDeltaWriter::del_path(uint16_t layer, uint16_t purpose,
                                  std::span<const Point2D> pts, int32_t half_w) {
    write_op_header(OpCode::DEL_PATH, layer, purpose);
    write_path_payload(pts, half_w);
    ++op_count_;
}

void BinaryDeltaWriter::write_path_payload(std::span<const Point2D> pts, int32_t half_w) {
    assert(pts.size() <= 65535);
    write_u16(static_cast<uint16_t>(pts.size()));
    write_i32(half_w);
    for (const auto& p : pts) { write_i32(p.x); write_i32(p.y); }
}

// ─── Via ops ──────────────────────────────────────────────────────────────────
void BinaryDeltaWriter::add_via(int32_t x, int32_t y, uint16_t via_def_idx) {
    write_op_header(OpCode::ADD_VIA, 0, 0);
    write_i32(x); write_i32(y); write_u16(via_def_idx);
    ++op_count_;
}

void BinaryDeltaWriter::del_via(int32_t x, int32_t y, uint16_t via_def_idx) {
    write_op_header(OpCode::DEL_VIA, 0, 0);
    write_i32(x); write_i32(y); write_u16(via_def_idx);
    ++op_count_;
}

// ─── Property ─────────────────────────────────────────────────────────────────
void BinaryDeltaWriter::set_prop(std::string_view key, std::string_view value) {
    write_op_header(OpCode::SET_PROP, 0, 0);
    assert(key.size() <= 255 && value.size() <= 65535);
    uint8_t klen = static_cast<uint8_t>(key.size());
    uint16_t vlen = static_cast<uint16_t>(value.size());
    write_bytes(&klen, 1);
    write_bytes(key.data(), key.size());
    write_u16(vlen);
    write_bytes(value.data(), value.size());
    ++op_count_;
}

// ─── Finish ───────────────────────────────────────────────────────────────────
void BinaryDeltaWriter::finish() {
    if (finished_) return;
    // Back-fill op_count at byte offset 6
    file_.seekp(static_cast<std::streamoff>(OP_COUNT_OFFSET), std::ios::beg);
    write_u32(op_count_);
    file_.flush();
    finished_ = true;
}

// ─── Low-level I/O ────────────────────────────────────────────────────────────
void BinaryDeltaWriter::write_op_header(OpCode op, uint16_t layer, uint16_t purpose) {
    auto opbyte = static_cast<uint8_t>(op);
    write_bytes(&opbyte, 1);
    write_u16(layer);
    write_u16(purpose);
}

void BinaryDeltaWriter::write_i32(int32_t v) {
    write_bytes(&v, 4);
}

void BinaryDeltaWriter::write_u16(uint16_t v) {
    write_bytes(&v, 2);
}

void BinaryDeltaWriter::write_u32(uint32_t v) {
    write_bytes(&v, 4);
}

void BinaryDeltaWriter::write_bytes(const void* data, std::size_t n) {
    file_.write(static_cast<const char*>(data), static_cast<std::streamsize>(n));
}

}  // namespace eda
