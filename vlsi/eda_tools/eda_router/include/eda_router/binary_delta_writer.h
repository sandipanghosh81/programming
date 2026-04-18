#pragma once
/*
 * binary_delta_writer.h  —  Compact flat-binary delta file encoder
 * ─────────────────────────────────────────────────────────────────────────────
 * Serialises the *diff* between the current EDA database state and the C++
 * router's computed result into a compact binary file stored on the shared
 * Docker volume (/eda_share).  The HEA Apply EU reads this file and replays
 * the operations inside the native EDA host (Virtuoso/ICC2/KLayout).
 *
 * WHY NOT JSON / OASIS FOR THE DELTA?
 *   OASIS is for the DRC gate (read by KLayout).
 *   The delta file is optimised for the HEA Apply tight loop — it must be
 *   parseable by a SKILL/Tcl script with minimal overhead.  A simple binary
 *   format with a fixed-size record header is 5–10× faster to parse than JSON
 *   and avoids the OASIS parser dependency inside the EDA host.
 *
 * FILE FORMAT  (little-endian, all integers):
 * ┌─────────────────────────────────────────────────────────────────────────┐
 * │  HEADER  (16 bytes)                                                     │
 * │    magic[4]        = "EDAB" (0x42414445)                                │
 * │    version[2]      = 0x0001                                             │
 * │    op_count[4]     = number of operations that follow                   │
 * │    reserved[6]     = 0                                                  │
 * ├─────────────────────────────────────────────────────────────────────────┤
 * │  OPERATIONS  (variable length, op_count entries)                        │
 * │  Each operation:                                                         │
 * │    opcode[1]   = OpCode enum value (see below)                          │
 * │    layer[2]    = layer index                                             │
 * │    purpose[2]  = purpose index (datatype)                               │
 * │    payload[?]  = depends on opcode:                                     │
 * │                                                                          │
 * │    ADD_RECT:   x[4] y[4] w[4] h[4]                           (16 B)    │
 * │    DEL_RECT:   x[4] y[4] w[4] h[4]                           (16 B)    │
 * │    ADD_PATH:   npts[2] half_w[4] x0[4] y0[4] ... xn[4] yn[4]           │
 * │    DEL_PATH:   npts[2] half_w[4] x0[4] y0[4] ... xn[4] yn[4]           │
 * │    ADD_VIA:    x[4] y[4] via_def_idx[2]                      (10 B)    │
 * │    DEL_VIA:    x[4] y[4] via_def_idx[2]                      (10 B)    │
 * │    SET_PROP:   key_len[1] key[?] val_len[2] val[?]  (string property)  │
 * └─────────────────────────────────────────────────────────────────────────┘
 *
 * USAGE:
 *   BinaryDeltaWriter w("/eda_share/delta_001.bin");
 *   w.add_rect(31, 0, 1000, 2000, 400, 200);
 *   w.add_path(31, 0, {{100,200},{300,400}}, 50);
 *   w.add_via(1000, 2000, 7);
 *   w.finish();
 */

#include <cstdint>
#include <string>
#include <string_view>
#include <span>
#include <fstream>
#include <expected>
#include <vector>

namespace eda {

struct DeltaError { std::string message; };
struct Point2D;   // forward — defined in oasis_writer.h

// ─── Operation opcodes ────────────────────────────────────────────────────────
enum class OpCode : uint8_t {
    ADD_RECT = 0x01,
    DEL_RECT = 0x02,
    ADD_PATH = 0x03,
    DEL_PATH = 0x04,
    ADD_VIA  = 0x05,
    DEL_VIA  = 0x06,
    SET_PROP = 0x07,
};

class BinaryDeltaWriter {
public:
    [[nodiscard]] static std::expected<BinaryDeltaWriter, DeltaError>
        open(std::string_view path);

    BinaryDeltaWriter(BinaryDeltaWriter&&) noexcept;
    BinaryDeltaWriter& operator=(BinaryDeltaWriter&&) noexcept;
    ~BinaryDeltaWriter();

    BinaryDeltaWriter(const BinaryDeltaWriter&) = delete;
    BinaryDeltaWriter& operator=(const BinaryDeltaWriter&) = delete;

    void add_rect(uint16_t layer, uint16_t purpose,
                  int32_t x, int32_t y, int32_t w, int32_t h);
    void del_rect(uint16_t layer, uint16_t purpose,
                  int32_t x, int32_t y, int32_t w, int32_t h);

    void add_path(uint16_t layer, uint16_t purpose,
                  std::span<const Point2D> pts, int32_t half_w);
    void del_path(uint16_t layer, uint16_t purpose,
                  std::span<const Point2D> pts, int32_t half_w);

    void add_via(int32_t x, int32_t y, uint16_t via_def_idx);
    void del_via(int32_t x, int32_t y, uint16_t via_def_idx);

    void set_prop(std::string_view key, std::string_view value);

    /// Rewind to header and write final op_count, then close file.
    void finish();

    [[nodiscard]] uint32_t op_count() const noexcept { return op_count_; }

private:
    explicit BinaryDeltaWriter(std::fstream file, std::string path);

    void write_op_header(OpCode op, uint16_t layer, uint16_t purpose);
    void write_i32(int32_t v);
    void write_u16(uint16_t v);
    void write_u32(uint32_t v);
    void write_bytes(const void* data, std::size_t n);
    void write_path_payload(std::span<const Point2D> pts, int32_t half_w);

    std::fstream  file_;
    std::string   path_;
    bool          finished_ = false;
    uint32_t      op_count_ = 0;
    std::streampos header_op_count_offset_;  // where to back-fill op_count
};

}  // namespace eda
