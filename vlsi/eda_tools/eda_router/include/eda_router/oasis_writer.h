#pragma once
/*
 * oasis_writer.h  —  Streaming OASIS layout encoder
 * ─────────────────────────────────────────────────────────────────────────────
 * Produces a standards-compliant OASIS (SEMI P39) file from routing/placement
 * geometry without any external library dependency.  The writer is append-only
 * (streaming) so memory usage is O(1) regardless of shape count.
 *
 * DESIGN PATTERNS:
 *   Builder  — call begin_cell() / write_rect() / write_path() / end_cell()
 *              then finish() to flush tables and CRC.
 *   RAII     — destructor calls finish() if not already called.
 *
 * C++ FEATURES:
 *   std::string_view  — zero-copy string parameters
 *   std::span<T>      — zero-copy coordinate spans
 *   std::expected     — error returns without exceptions
 *
 * BINARY FORMAT SUMMARY (SEMI P39):
 *   Magic + version header (13 bytes)
 *   Zero or more CELL / PLACEMENT / GEOMETRY records
 *   NAME TABLE (layer/net name strings)
 *   END record + CRC32 (4 bytes)
 *
 * Each geometry record uses delta-encoded coordinates relative to the previous
 * shape on the same layer, reducing file size ~3–5× vs absolute coordinates.
 *
 * USAGE:
 *   OASISWriter w("/eda_share/routing_001.oas");
 *   w.begin_cell("MYDESIGN");
 *   w.write_rect(31, 0, 1000, 2000, 400, 200, "VDD");
 *   w.write_path(31, 0, {{100,200},{300,200},{300,400}}, 50, "net_01");
 *   w.end_cell();
 *   w.finish();
 */

#include <cstdint>
#include <string>
#include <string_view>
#include <span>
#include <unordered_map>
#include <vector>
#include <fstream>
#include <expected>

namespace eda {

// ─── Error type ───────────────────────────────────────────────────────────────
struct OASISError {
    std::string message;
};

// ─── Coordinate pair ─────────────────────────────────────────────────────────
struct Point2D { int32_t x; int32_t y; };

// ─── Writer class ─────────────────────────────────────────────────────────────
class OASISWriter {
public:
    /// Open/create the output file.  Writes the OASIS magic + version header.
    [[nodiscard]] static std::expected<OASISWriter, OASISError>
        open(std::string_view path);

    OASISWriter(OASISWriter&&) noexcept;
    OASISWriter& operator=(OASISWriter&&) noexcept;
    ~OASISWriter();  // calls finish() if not already done

    // Non-copyable
    OASISWriter(const OASISWriter&) = delete;
    OASISWriter& operator=(const OASISWriter&) = delete;

    /// Start a new cell (design block).  Must be called before any write_*.
    void begin_cell(std::string_view cell_name);

    /// Write a rectangle.  All dimensions in nm (database units).
    void write_rect(int layer_idx, int purpose_idx,
                    int32_t x, int32_t y,
                    int32_t width, int32_t height,
                    std::string_view net_name = "");

    /// Write a path (wire).  Points are the wire centreline.  half_width in nm.
    void write_path(int layer_idx, int purpose_idx,
                    std::span<const Point2D> points,
                    int32_t half_width,
                    std::string_view net_name = "");

    /// Close the current cell record.
    void end_cell();

    /// Flush name tables, write END record + CRC32, close file.
    /// Idempotent — safe to call multiple times.
    void finish();

    /// Total shapes written so far.
    [[nodiscard]] std::size_t shape_count() const noexcept { return shape_count_; }

private:
    explicit OASISWriter(std::ofstream file, std::string path);

    // Low-level encoding helpers
    void write_byte(uint8_t b);
    void write_uint(uint64_t v);   // OASIS unsigned-LEB128
    void write_int(int64_t v);     // OASIS signed-LEB128
    void write_bytes(const void* data, std::size_t n);
    void write_string_table();
    void write_end_record();

    /// Intern a layer/net name → index.
    [[nodiscard]] uint32_t intern_layer(std::string_view name);
    [[nodiscard]] uint32_t intern_net(std::string_view name);

    std::ofstream file_;
    std::string   path_;
    bool          finished_ = false;
    bool          cell_open_ = false;
    std::size_t   shape_count_ = 0;

    // Name tables built incrementally; flushed in finish()
    std::unordered_map<std::string, uint32_t> layer_names_;
    std::unordered_map<std::string, uint32_t> net_names_;
    std::vector<std::string> layer_name_list_;
    std::vector<std::string> net_name_list_;

    // Delta-encoding state per layer (last x/y written)
    std::unordered_map<int, Point2D> last_pos_;

    // CRC accumulator
    uint32_t crc_ = 0xFFFFFFFF;
    void crc_update(const void* data, std::size_t n);
};

}  // namespace eda
