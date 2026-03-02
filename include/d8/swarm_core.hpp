/*
 * DECIMA-8 Source Code
 * This code is part of Decima-8 Core
 *
 * All rights belong to the ORDEN (c) 2026
 */

#pragma once

#include <array>
#include <vector>
#include <cstdint>
#include <chrono>
#include "swarm.hpp"
#include "shared_buffer.hpp"

namespace d8 {

    // Ядро (синхронная реализация): все операции выполняются в вызывающем потоке
    // Нет внутренних потоков, условных переменных или атомарных флагов для синхронизации
    // Все методы блокирующие и возвращают управление только после завершения операции

    class swarm_core {
    public:
        swarm_core() = default;
        ~swarm_core() = default;

        // Установить shared buffer для телеметрии
        void set_shared_buffer(shared_telemetry_buffer* buf) noexcept {
            shared_buf_ = buf;
            if (swarm_) {
                swarm_->set_shared_buffer(buf);
            }
        }

        // Bake blob (блокирующий вызов, выполняется мгновенно)
        status ev_bake(std::span<const u8> blob) {
            const auto start_time = std::chrono::steady_clock::now();
            if (!swarm_) return { status::code::BadPhase, 0, "Swarm not initialized" };
            auto st = swarm_->ev_bake(blob);
            if (st) {
                bake_blob_.assign(blob.begin(), blob.end());
                bake_applied_ = true;
                if (shared_buf_) {
                    swarm_->set_shared_buffer(shared_buf_);
                }
            }
            return st;
        }

        // Reset domain (блокирующий вызов, выполняется мгновенно)
        status ev_reset_domain(u16 mask16) {
            if (!swarm_) return { status::code::BadPhase, 0, "Swarm not initialized" };
            return swarm_->ev_reset_domain(mask16);
        }

        // Синхронный шаг выполнения одного flash с текущим ingress
        // Возвращает результат операции flash
        status step(u32 frame_tag) {
            if (!swarm_ || !bake_applied_) {
                return { status::code::NotBaked, 0, "Swarm not baked" };
            }

            auto res = swarm_->ev_flash(frame_tag, vsb_ingress_);

            last_flags_ = res.flags32_last;

            return res.st;
        }

        // Установить VSB ingress и немедленно выполнить один шаг flash
        // Возвращает результат операции flash
        status set_vsb_ingress_and_step(const std::array<u8, kLanes>& ingress, u32 frame_tag) {
            vsb_ingress_ = ingress;
            return step(frame_tag);
        }

        // Только устанавливает ingress БЕЗ выполнения шага (для случаев, когда шаг будет вызван отдельно)
        void set_vsb_ingress(const std::array<u8, kLanes>& ingress) noexcept {
            vsb_ingress_ = ingress;
        }

        // Получить последний результат flash
        u32 get_last_flags() const noexcept {
            return last_flags_;
        }

        // Проверка активных тайлов
        std::size_t active_tile_count() const noexcept {
            return swarm_ ? swarm_->active_tile_count() : kTileCount;
        }

        bool double_strait_bake() const noexcept {
            return swarm_ ? swarm_->double_strait_bake() : false;
        }

        // Получить доступ к swarm для чтения данных (для IDE)
        swarm* get_swarm() noexcept {
            return swarm_.get();
        }

        const swarm* get_swarm() const noexcept {
            return swarm_.get();
        }

        // Установить параметры тайла (для IDE)
        status set_tile_params(std::size_t tile_id, i16 thr_lo, i16 thr_hi, u16 decay16, u8 domain_id, u8 priority, u16 pattern_id) {
            status result;
            {
                if (!swarm_) return { status::code::BadPhase, 0, "Swarm not initialized" };
                result = swarm_->set_tile_params(tile_id, thr_lo, thr_hi, decay16, domain_id, priority);
                if (!result) return result;
                result = swarm_->set_tile_pattern_id(tile_id, pattern_id);
                if (result) {
                    auto updated_blob = swarm_->serialize_current_bake();
                    if (!updated_blob.empty()) {
                        bake_blob_ = std::move(updated_blob);
                        auto bake_st = swarm_->ev_bake(bake_blob_);
                        if (bake_st) {
                            bake_applied_ = true;
                        }
                        result = bake_st;
                    }
                }
            }
            return result;
        }

        status set_tile_pattern_id(std::size_t tile_id, u16 pattern_id) {
            status result;
            if (!swarm_) return { status::code::BadPhase, 0, "Swarm not initialized" };
            result = swarm_->set_tile_pattern_id(tile_id, pattern_id);
            if (result) {
                auto updated_blob = swarm_->serialize_current_bake();
                if (!updated_blob.empty()) {
                    bake_blob_ = std::move(updated_blob);
                    auto bake_st = swarm_->ev_bake(bake_blob_);
                    if (bake_st) {
                        bake_applied_ = true;
                    }
                    result = bake_st;
                }
            }
            return result;
        }

        status set_tile_routing_masks(std::size_t tile_id, u8 maskN, u8 maskE, u8 maskS, u8 maskW,
            u8 maskNE, u8 maskSE, u8 maskSW, u8 maskNW, u8 bus_w, u8 bus_r) {
            status result;
            if (!swarm_) return { status::code::BadPhase, 0, "Swarm not initialized" };
            result = swarm_->set_tile_routing_masks(tile_id, maskN, maskE, maskS, maskW,
                maskNE, maskSE, maskSW, maskNW, bus_w, bus_r);
            if (result) {
                auto updated_blob = swarm_->serialize_current_bake();
                if (!updated_blob.empty()) {
                    bake_blob_ = std::move(updated_blob);
                    auto bake_st = swarm_->ev_bake(bake_blob_);
                    if (bake_st) {
                        bake_applied_ = true;
                    }
                    result = bake_st;
                }
            }
            return result;
        }

        status set_tile_weight_sign(std::size_t tile_id, std::size_t weight_idx, bool sign) {
            if (!swarm_) return { status::code::BadPhase, 0, "Swarm not initialized" };
            return swarm_->set_tile_weight_sign(tile_id, weight_idx, sign);
        }

        status set_tile_weight_mag(std::size_t tile_id, std::size_t weight_idx, u8 mag) {
            if (!swarm_) return { status::code::BadPhase, 0, "Swarm not initialized" };
            return swarm_->set_tile_weight_mag(tile_id, weight_idx, mag);
        }

        status set_tile_field_limit(u32 limit) {
            status result;            
            if (!swarm_) return { status::code::BadPhase, 0, "Swarm not initialized" };
            if (!bake_applied_) return { status::code::NotBaked, 0, "NotBaked" };
            result = swarm_->set_tile_field_limit(limit);
            if (!result) return result;
            auto updated_blob = swarm_->serialize_current_bake();
            if (!updated_blob.empty()) {
                bake_blob_ = std::move(updated_blob);
                auto bake_st = swarm_->ev_bake(bake_blob_);
                if (bake_st) {
                    bake_applied_ = true;
                }
                result = bake_st;
            }
            return result;
        }

        status set_tile_reset_on_fire_mask16(std::size_t tile_id, u16 reset_on_fire_mask16) {
            status result;
            if (!swarm_) return { status::code::BadPhase, 0, "Swarm not initialized" };
            result = swarm_->set_tile_reset_on_fire_mask16(tile_id, reset_on_fire_mask16);
            if (result) {
                auto updated_blob = swarm_->serialize_current_bake();
                if (!updated_blob.empty()) {
                    bake_blob_ = std::move(updated_blob);
                    auto bake_st = swarm_->ev_bake(bake_blob_);
                    if (bake_st) {
                        bake_applied_ = true;
                    }
                    result = bake_st;
                }
            }
            return result;
        }

        status set_tile_locked(std::size_t tile_id, bool locked) {
            if (!swarm_) return { status::code::BadPhase, 0, "Swarm not initialized" };
            return swarm_->set_tile_locked(tile_id, locked);
        }

        bool get_tile_locked(std::size_t tile_id) const {
            if (!swarm_) return false;
            return swarm_->get_tile_locked(tile_id);
        }

        swarm::tile_params get_tile_params(std::size_t tile_id) const {
            if (!swarm_) return {};
            return swarm_->get_tile_params(tile_id);
        }

        std::vector<u8> serialize_current_bake() {
            if (!swarm_ || !bake_applied_) {
                return {};
            }
            return swarm_->serialize_current_bake();
        }

        void set_double_strait_bake(bool enabled) {
            if (swarm_) {
                swarm_->set_double_strait_bake(enabled);
            }
        }

    private:
        std::unique_ptr<swarm> swarm_{ std::make_unique<swarm>() };
        shared_telemetry_buffer* shared_buf_{ nullptr };

        std::array<u8, kLanes> vsb_ingress_{};
        bool bake_applied_{ false };
        std::vector<u8> bake_blob_;

        u32 last_flags_{ 0 };
    };

} // namespace d8
