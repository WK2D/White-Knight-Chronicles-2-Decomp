// Authored box occlusion against quadrilateral shadow volumes.
// Owned typed data and injected numeric policy; no guest execution interface.
#ifndef WKC_NATIVE_OCCLUSION_H
#define WKC_NATIVE_OCCLUSION_H

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace wkc::visibility::occlusion {

using Vec3 = std::array<float, 3>;
using AffineRows = std::array<std::array<float, 4>, 3>;
using QueryColumns = std::array<Vec3, 4>;
using OccluderCorners = std::array<Vec3, 4>;

struct Bounds {
    Vec3 center;
    Vec3 half_size;
};
struct Plane {
    Vec3 normal;
    float offset = 0;
};
struct PreparedOccluder {
    float maximum_z = 0;
    // The face first, then side planes in the original rejection order.
    std::array<Plane, 5> planes;
};
struct PreparedScene {
    std::array<PreparedOccluder, 128> occluders;
    std::size_t count = 0;
};
struct PreparedBox {
    Vec3 center;
    std::array<Vec3, 3> half_axes;
    float minimum_z_bound = 0;
};

// Math is a pure scalar policy with add, subtract, multiply,
// fused_multiply_add(a,b,c), negative_multiply_subtract(a,b,c) = c-a*b,
// interpolated_reciprocal_sqrt(x), and ordered greater(a,b).
// The reciprocal-sqrt policy returns the estimate after interpolation, before
// the refinement below. Supplying std::sqrt instead is not an exact oracle.
// All operations used here have the arithmetic grouping recovered from the
// original. No guest registers, memory readers, local store or SDK types enter.
template<class Math> struct Kernel {
    static float absolute(float value) {
        return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) & 0x7fffffffu);
    }
    static float negate(float value) {
        return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) ^ 0x80000000u);
    }
    static Vec3 difference(const Vec3& a, const Vec3& b) {
        Vec3 result;
        for (unsigned i = 0; i < 3; ++i) result[i] = Math::subtract(a[i], b[i]);
        return result;
    }
    static Vec3 scaled(const Vec3& value, float factor) {
        Vec3 result;
        for (unsigned i = 0; i < 3; ++i) result[i] = Math::multiply(value[i], factor);
        return result;
    }
    static float dot(const Vec3& a, const Vec3& b) {
        const auto x = Math::multiply(a[0], b[0]);
        const auto xy = Math::fused_multiply_add(a[1], b[1], x);
        return Math::fused_multiply_add(a[2], b[2], xy);
    }
    static float face_distance(const Vec3& normal_value, const Vec3& point) {
        const auto x = Math::multiply(normal_value[0], point[0]);
        const auto xy = Math::fused_multiply_add(point[1], normal_value[1], x);
        return negate(Math::fused_multiply_add(point[2], normal_value[2], xy));
    }
    static Vec3 cross(const Vec3& a, const Vec3& b) {
        Vec3 result;
        for (unsigned i = 0; i < 3; ++i) {
            const auto y = (i + 1) % 3, z = (i + 2) % 3;
            result[i] = Math::negative_multiply_subtract(
                a[z], b[y], Math::multiply(a[y], b[z]));
        }
        return result;
    }
    static float refined_inverse_length(float length_squared) {
        const auto inverse = Math::interpolated_reciprocal_sqrt(length_squared);
        const auto length = Math::multiply(inverse, length_squared);
        const auto residual = Math::negative_multiply_subtract(inverse, length,
            std::bit_cast<float>(0x3f800001u));
        return Math::fused_multiply_add(residual, Math::multiply(inverse, 0.5f), inverse);
    }
    static float refined_length(float length_squared) {
        const auto inverse = Math::interpolated_reciprocal_sqrt(length_squared);
        const auto length = Math::multiply(inverse, length_squared);
        const auto residual = Math::negative_multiply_subtract(inverse, length,
            std::bit_cast<float>(0x3f800001u));
        return Math::fused_multiply_add(residual, Math::multiply(length, 0.5f), length);
    }
    static Vec3 normal(const Vec3& value) {
        return scaled(value, refined_inverse_length(dot(value, value)));
    }
    static Vec3 query_point(const QueryColumns& query, const Vec3& point) {
        Vec3 result;
        for (unsigned i = 0; i < 3; ++i) {
            const auto xz = Math::fused_multiply_add(query[2][i], point[2],
                Math::multiply(query[0][i], point[0]));
            const auto yt = Math::fused_multiply_add(query[1][i], point[1], query[3][i]);
            result[i] = Math::add(xz, yt);
        }
        return result;
    }
    static PreparedOccluder prepare_occluder(const QueryColumns& query,
                                              const OccluderCorners& corners) {
        std::array<Vec3, 4> point;
        for (unsigned i = 0; i < 4; ++i) point[i] = query_point(query, corners[i]);
        PreparedOccluder result;
        result.maximum_z = point[0][2];
        for (unsigned i = 1; i < 4; ++i)
            if (Math::greater(point[i][2], result.maximum_z)) result.maximum_z = point[i][2];
        auto face = normal(cross(difference(point[1], point[2]),
                                 difference(point[0], point[2])));
        auto face_offset = face_distance(face, point[0]);
        if (Math::greater(face_offset, 0.0f)) {
            // Recompute from these differences; simply negating the first
            // normal changes the rounding of the original positive branch.
            face = normal(cross(difference(point[1], point[0]),
                                difference(point[2], point[0])));
            face_offset = face_distance(face, point[0]);
            result.planes[1].normal = normal(cross(point[1], point[2]));
            result.planes[2].normal = normal(cross(point[3], point[0]));
            result.planes[3].normal = normal(cross(point[0], point[1]));
            result.planes[4].normal = normal(cross(point[2], point[3]));
        } else {
            result.planes[1].normal = normal(cross(point[2], point[1]));
            result.planes[2].normal = normal(cross(point[0], point[3]));
            result.planes[3].normal = normal(cross(point[1], point[0]));
            result.planes[4].normal = normal(cross(point[3], point[2]));
        }
        result.planes[0] = {face, face_offset};
        return result;
    }
    static bool prepare_scene(const QueryColumns& query,
                              std::span<const OccluderCorners> corners,
                              PreparedScene& output) {
        if (corners.size() > 128) return false;
        PreparedScene next{};
        next.count = corners.size();
        for (std::size_t i = 0; i < corners.size(); ++i)
            next.occluders[i] = prepare_occluder(query, corners[i]);
        output = std::move(next);
        return true;
    }
    static PreparedBox prepare_box(const QueryColumns& query,
                                    const AffineRows& object, const Bounds& bounds) {
        std::array<Vec3, 4> combined;
        for (unsigned column = 0; column < 4; ++column)
            for (unsigned i = 0; i < 3; ++i) {
                const auto xz = Math::fused_multiply_add(query[2][i], object[2][column],
                    Math::multiply(query[0][i], object[0][column]));
                const auto yt = Math::fused_multiply_add(query[3][i], column == 3 ? 1.0f : 0.0f,
                    Math::multiply(query[1][i], object[1][column]));
                combined[column][i] = Math::add(xz, yt);
            }
        PreparedBox result;
        for (unsigned i = 0; i < 3; ++i) {
            const auto xz = Math::fused_multiply_add(combined[2][i], bounds.center[2],
                Math::multiply(combined[0][i], bounds.center[0]));
            const auto yt = Math::fused_multiply_add(combined[1][i], bounds.center[1], combined[3][i]);
            result.center[i] = Math::add(xz, yt);
        }
        for (unsigned i = 0; i < 3; ++i) result.half_axes[i] = scaled(combined[i], bounds.half_size[i]);
        // This early bound uses the length of the original local half-size,
        // not the transformed half-axis lengths. Preserve the selected mode.
        result.minimum_z_bound = Math::subtract(result.center[2],
            refined_length(dot(bounds.half_size, bounds.half_size)));
        return result;
    }
    static bool fully_inside(const PreparedBox& box, const Plane& plane, bool face) {
        const auto x = absolute(dot(plane.normal, box.half_axes[0]));
        const auto y = absolute(dot(plane.normal, box.half_axes[1]));
        const auto z = absolute(dot(plane.normal, box.half_axes[2]));
        const auto radius = Math::add(Math::add(x, y), z);
        const auto distance = dot(plane.normal, box.center);
        const auto limit = face ? Math::add(plane.offset, distance) : distance;
        // Equality is inside. This is intentionally the negation of an ordered
        // greater comparison, rather than substituting an IEEE <= comparison.
        return !Math::greater(radius, limit);
    }
    static bool visible(const PreparedScene& scene, const PreparedBox& box) {
        for (std::size_t i = 0; i < scene.count; ++i) {
            const auto& occluder = scene.occluders[i];
            if (Math::greater(box.minimum_z_bound, occluder.maximum_z)) continue;
            bool contained = true;
            for (unsigned plane = 0; plane < 5; ++plane)
                if (!fully_inside(box, occluder.planes[plane], plane == 0)) {
                    contained = false;
                    break;
                }
            if (contained) return false;
        }
        return true;
    }
};

} // namespace wkc::visibility::occlusion
#endif
