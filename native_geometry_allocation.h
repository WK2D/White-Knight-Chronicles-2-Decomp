#ifndef WKC_NATIVE_GEOMETRY_ALLOCATION_H
#define WKC_NATIVE_GEOMETRY_ALLOCATION_H

#include <cstdint>

// Project-written ring arithmetic and resumable allocation protocol. The
// caller owns shared memory, slot leases and ordered operation receipts.
namespace wkc::geometry::allocation {

struct RingState {
    std::uint32_t begin;
    std::uint32_t mapped_base;
    std::uint32_t end;
    std::uint32_t producer;
    std::uint32_t location;
    std::uint32_t consumer_address;
};

struct RingRequest {
    // Already rounded by the caller. This kernel deliberately performs no
    // alignment, validation, narrowing, or extra capacity checks.
    std::uint32_t bytes;
};

struct RingConsumerReceipt {
    // A completed, ordered read of the u32 at RingState::consumer_address.
    std::uint32_t cursor;
};

enum class RingDecisionKind {
    reserved,
    consumer_wait,
    capacity_rejected
};

struct RingAllocation {
    std::uint32_t effective_address;
    std::uint32_t mapped_address;
    std::uint32_t location;
};

struct RingDecision {
    RingDecisionKind kind;
    // Only a reserved decision changes producer. The other state words are
    // preserved; publishing the complete original block remains the caller's
    // responsibility, including all words outside this typed view.
    RingState state;
    // Defined only when kind == reserved. No result should be written for a
    // consumer_wait or capacity_rejected decision.
    RingAllocation allocation;
    bool wrapped;
};

// Inspect before issuing the external consumer read: the original capacity
// rejection returns before performing that read. All arithmetic is explicitly
// u32, including doubling the request and subtracting the limits.
[[nodiscard]] constexpr bool ring_requires_consumer(
    const RingState& state, RingRequest request) noexcept
{
    const std::uint32_t doubled = request.bytes + request.bytes;
    const std::uint32_t capacity = state.end - state.begin;
    return doubled <= capacity;
}

// One pure readiness decision. A waiting caller may either try its other
// allocator or suspend and acquire another real receipt before retrying.
// This function implements neither a blocking loop nor shared publication.
[[nodiscard]] constexpr RingDecision decide_ring_reservation(
    const RingState& state, RingRequest request,
    RingConsumerReceipt receipt) noexcept
{
    RingDecision decision{RingDecisionKind::capacity_rejected, state, {}, false};
    if (!ring_requires_consumer(state, request))
        return decision;

    const std::uint32_t contiguous_end = state.producer + request.bytes;
    const std::uint32_t wrapped_end = state.begin + request.bytes;
    const bool wrap = contiguous_end > state.end;

    // The wrapped range needs the consumer strictly beyond its end. The
    // contiguous range rejects a consumer in (producer, contiguous_end].
    // These are unsigned comparisons of the wrapped arithmetic values; do not
    // turn them into a wider range-overlap calculation.
    const bool wrapped_range_available = !wrap || receipt.cursor > wrapped_end;
    const bool consumer_inside_contiguous_range =
        receipt.cursor > state.producer && receipt.cursor <= contiguous_end;
    if (!wrapped_range_available || consumer_inside_contiguous_range) {
        decision.kind = RingDecisionKind::consumer_wait;
        decision.wrapped = wrap;
        return decision;
    }

    const std::uint32_t start = wrap ? state.begin : state.producer;
    decision.kind = RingDecisionKind::reserved;
    decision.state.producer = wrap ? wrapped_end : contiguous_end;
    decision.allocation = {
        start,
        state.mapped_base + (start - state.begin),
        state.location
    };
    decision.wrapped = wrap;
    return decision;
}

} // namespace wkc::geometry::allocation


#include <array>
#include <cstdint>
#include <optional>

namespace wkc::geometry::allocation {

using AllocationLine = std::array<std::uint8_t, 128>;
using AllocationRecord = std::array<std::uint8_t, 32>;

[[nodiscard]] constexpr std::uint32_t allocation_word(
    const auto& bytes, unsigned offset) noexcept
{
    return (std::uint32_t(bytes[offset]) << 24) |
           (std::uint32_t(bytes[offset + 1]) << 16) |
           (std::uint32_t(bytes[offset + 2]) << 8) |
           std::uint32_t(bytes[offset + 3]);
}

constexpr void allocation_word(auto& bytes, unsigned offset,
                               std::uint32_t word) noexcept
{
    bytes[offset] = static_cast<std::uint8_t>(word >> 24);
    bytes[offset + 1] = static_cast<std::uint8_t>(word >> 16);
    bytes[offset + 2] = static_cast<std::uint8_t>(word >> 8);
    bytes[offset + 3] = static_cast<std::uint8_t>(word);
}

enum class AllocationLineKind { slot, global };

struct AllocationLineReceipt {
    AllocationLine bytes;
    std::uint32_t atomic_status;
    // Backend-owned reservation/ownership identity. A slot receipt must retain
    // exclusive slot ownership until this transaction finishes, even while a
    // global reservation replaces the architectural lock-line reservation.
    std::uint64_t token;
};

struct AllocationRequest {
    std::uint32_t control;
    std::uint32_t bytes;
    std::uint32_t slot;
    std::uint32_t consumer_dma_tag;
};

enum class AllocationProgress { progressed, pending, finished };

// Backend contract (all addresses and arithmetic are u32):
// optional<u32> read_machine_status();
// void set_interrupts(bool);
// optional<AllocationLineReceipt> get_locked_line(address, kind);
// void fence();
// optional<u32> put_conditional(address, const AllocationLine&, token);
// optional<u32> put_unconditional(address, const AllocationLine&, token);
// optional<RingConsumerReceipt> get_consumer(address, dma_tag);
// bool wait_decrementer_reads(unsigned count);
//
// Each optional response is a COMPLETED operation receipt. If it is pending,
// subsequent calls to that same method must poll/complete the outstanding
// operation, not submit duplicates. get_consumer owns GET4, tag-mask/update-all
// and tag-status completion; the explicit fence follows the receipt. Atomic
// line methods own the atomic command and its status read. The backend must
// preserve 128-byte line alignment semantics, exclusivity, memory visibility
// and its ownership-token lifetime. wait_decrementer_reads(25) must represent
// the actual 25-read delay or a validated semantic scheduling adapter; returning
// true must not manufacture a new consumer receipt. A fresh GET follows it.
//
// This resumable transaction contains no retry spin loop. Call advance only
// when the backend can progress, or schedule after a pending response.
// Keep the transaction and backend alive under the same exclusive slot lease
// until finished(). Cancellation requires backend-specific cleanup; destroying
// a pending transaction does not release reservations or restore interrupts.
class AllocationTransaction {
public:
    explicit constexpr AllocationTransaction(
        AllocationRequest request, AllocationRecord initial_result) noexcept
        : request_(request), result_(initial_result) {}

    AllocationTransaction(const AllocationTransaction&) = delete;
    AllocationTransaction& operator=(const AllocationTransaction&) = delete;
    AllocationTransaction(AllocationTransaction&&) = delete;
    AllocationTransaction& operator=(AllocationTransaction&&) = delete;

    [[nodiscard]] constexpr const AllocationRecord& result() const noexcept {
        return result_;
    }
    [[nodiscard]] constexpr bool finished() const noexcept { return phase_ == Phase::done; }
    [[nodiscard]] constexpr bool success() const noexcept { return finished() && success_; }
    [[nodiscard]] constexpr std::uint32_t rounded_bytes() const noexcept { return rounded_; }
    [[nodiscard]] constexpr const AllocationLine& slot_line() const noexcept { return slot_line_; }
    [[nodiscard]] constexpr const AllocationLine& global_line() const noexcept { return global_line_; }

    template<class Backend>
    AllocationProgress advance(Backend& backend) {
        switch (phase_) {
        case Phase::begin:
            // This byte is cleared before both direct and allocated branches.
            // No other byte of this quadword is implicitly initialized.
            result_[20] = 0;
            if (request_.control & 2u) {
                allocation_word(result_, 0, request_.control & ~3u);
                allocation_word(result_, 4, 0xffffffffu);
                allocation_word(result_, 8, request_.control & 1u);
                allocation_word(result_, 12, 0u);
                allocation_word(result_, 16, 0u);
                success_ = true;
                phase_ = Phase::done;
                return AllocationProgress::finished;
            }
            rounded_ = (request_.bytes + 127u) & ~127u;
            slot_address_ = request_.control + 128u + (request_.slot << 7);
            phase_ = Phase::machine_status;
            break;

        case Phase::machine_status: {
            auto status = backend.read_machine_status();
            if (!status) return AllocationProgress::pending;
            machine_status_ = *status;
            backend.set_interrupts(false);
            phase_ = Phase::slot_read;
            break;
        }

        case Phase::slot_read: {
            auto receipt = backend.get_locked_line(slot_address_, AllocationLineKind::slot);
            if (!receipt) return AllocationProgress::pending;
            slot_line_ = receipt->bytes;
            slot_token_ = receipt->token;
            backend.fence();
            if (allocation_word(slot_line_, 0) == allocation_word(slot_line_, 8)) {
                global_after_ = GlobalAfter::finish;
                phase_ = Phase::global_read;
            } else {
                blocking_ = false;
                phase_ = Phase::ring_capacity;
            }
            break;
        }

        case Phase::ring_capacity:
            if (!ring_requires_consumer(ring_state(), {rounded_})) {
                ring_success_ = false;
                ring_finished();
            } else {
                phase_ = Phase::ring_consumer;
            }
            break;

        case Phase::ring_consumer: {
            const auto ring = ring_state();
            auto receipt = backend.get_consumer(ring.consumer_address, request_.consumer_dma_tag);
            if (!receipt) return AllocationProgress::pending;
            backend.fence();
            auto decision = decide_ring_reservation(ring, {rounded_}, *receipt);
            if (decision.kind == RingDecisionKind::reserved) {
                allocation_word(slot_line_, 12, decision.state.producer);
                allocation_word(result_, 0, decision.allocation.effective_address);
                allocation_word(result_, 4, decision.allocation.mapped_address);
                allocation_word(result_, 8, decision.allocation.location);
                ring_success_ = true;
                ring_finished();
            } else if (blocking_) {
                phase_ = Phase::ring_delay;
            } else {
                ring_success_ = false;
                ring_finished();
            }
            break;
        }

        case Phase::ring_delay:
            if (!backend.wait_decrementer_reads(25)) return AllocationProgress::pending;
            phase_ = Phase::ring_consumer;
            break;

        case Phase::global_read: {
            auto receipt = backend.get_locked_line(request_.control, AllocationLineKind::global);
            if (!receipt) return AllocationProgress::pending;
            global_line_ = receipt->bytes;
            global_token_ = receipt->token;
            backend.fence();
            const std::uint32_t old = allocation_word(global_line_, 12);
            const std::uint32_t end = old + rounded_;
            if (end > allocation_word(global_line_, 8)) {
                global_start_ = 0;
                allocation_word(global_line_, 20,
                    allocation_word(global_line_, 20) + rounded_);
            } else {
                global_start_ = old;
                allocation_word(global_line_, 12, end);
                // This write happens before PUTLLC. A failed conditional store
                // does not roll it back; a subsequent overflow may preserve it.
                allocation_word(result_, 8, allocation_word(global_line_, 16));
            }
            backend.fence();
            phase_ = Phase::global_publish;
            break;
        }

        case Phase::global_publish: {
            auto status = backend.put_conditional(request_.control, global_line_, global_token_);
            if (!status) return AllocationProgress::pending;
            if (*status & 1u) {
                // A failed conditional store requires a genuinely new line
                // receipt. It does not retry the stale local candidate.
                phase_ = Phase::global_read;
                break;
            }
            allocation_word(result_, 0, global_start_);
            allocation_word(result_, 4, allocation_word(global_line_, 4) +
                (global_start_ - allocation_word(global_line_, 0)));
            success_ = global_start_ != 0;
            if (!success_ && global_after_ == GlobalAfter::blocking_ring) {
                blocking_ = true;
                phase_ = Phase::ring_capacity;
            } else {
                uses_ring_ = false;
                phase_ = Phase::restore_interrupts;
            }
            break;
        }

        case Phase::slot_publish: {
            // Fence once per command submission, not again for every pending
            // status poll. The state machine enters this phase only once per
            // unconditional publication.
            backend.fence();
            phase_ = Phase::slot_publish_wait;
            break;
        }

        case Phase::slot_publish_wait: {
            auto status = backend.put_unconditional(slot_address_, slot_line_, slot_token_);
            if (!status) return AllocationProgress::pending;
            // The original reads but does not inspect unconditional status.
            success_ = ring_success_;
            uses_ring_ = true;
            phase_ = Phase::restore_interrupts;
            break;
        }

        case Phase::restore_interrupts:
            // Restore only when the real machine-status receipt reports the
            // previous interrupt-enable bit. The backend must not derive this
            // value from a thread's running/stopped status.
            if (machine_status_ & 1u) backend.set_interrupts(true);
            if (success_) {
                allocation_word(result_, 12, uses_ring_
                    ? allocation_word(result_, 0) + rounded_ : 0u);
                allocation_word(result_, 16, uses_ring_
                    ? allocation_word(slot_line_, 20) : 0u);
            }
            phase_ = Phase::done;
            return AllocationProgress::finished;

        case Phase::done:
            return AllocationProgress::finished;
        }
        return AllocationProgress::progressed;
    }

private:
    enum class Phase {
        begin, machine_status, slot_read, ring_capacity, ring_consumer,
        ring_delay, global_read, global_publish, slot_publish, slot_publish_wait,
        restore_interrupts, done
    };
    enum class GlobalAfter { finish, blocking_ring };

    [[nodiscard]] constexpr RingState ring_state() const noexcept {
        return {allocation_word(slot_line_, 0), allocation_word(slot_line_, 4),
                allocation_word(slot_line_, 8), allocation_word(slot_line_, 12),
                allocation_word(slot_line_, 16), allocation_word(slot_line_, 20)};
    }

    constexpr void ring_finished() noexcept {
        if (ring_success_ || blocking_) {
            // Even an immediate capacity rejection from the blocking call is
            // followed by PUTLLUC of the unchanged slot block in the original.
            phase_ = Phase::slot_publish;
        } else {
            global_after_ = GlobalAfter::blocking_ring;
            phase_ = Phase::global_read;
        }
    }

    AllocationRequest request_;
    AllocationRecord result_;
    AllocationLine slot_line_{};
    AllocationLine global_line_{};
    Phase phase_ = Phase::begin;
    GlobalAfter global_after_ = GlobalAfter::finish;
    std::uint32_t rounded_ = 0, slot_address_ = 0, machine_status_ = 0, global_start_ = 0;
    std::uint64_t slot_token_ = 0, global_token_ = 0;
    bool blocking_ = false, ring_success_ = false, uses_ring_ = false, success_ = false;
};

} // namespace wkc::geometry::allocation

#endif
