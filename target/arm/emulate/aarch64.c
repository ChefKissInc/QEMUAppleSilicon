/*
 * ARM AARCH64 Fallback Emulation.
 *
 * Copyright (c) 2025-2026 Visual Ehrmanntraut (VisualEhrmanntraut).
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "exec/target_page.h"
#include "target/arm/emulate/aarch64.h"
#include "cpu-qom.h"
#include "cpu.h"
#include "system/hw_accel.h"
#include "system/memory.h"

// TODO: Protection checks? lol
static hwaddr arm_aarch64_fallback_emu_vtop(CPUState *cpu, vaddr addr)
{
    return cpu_get_phys_page_debug(cpu, addr & TARGET_PAGE_MASK) +
           (addr & ~TARGET_PAGE_MASK);
}

bool arm_aarch64_fallback_emu_single(CPUState *cpu,
                                     ArmAarch64FallbackEmuGetReg get_reg,
                                     ArmAarch64FallbackEmuSetReg set_reg)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;

    cpu_synchronize_state(cpu);

    uint32_t inst = 0;
    if (address_space_read(cpu->as, arm_aarch64_fallback_emu_vtop(cpu, env->pc),
                           MEMTXATTRS_UNSPECIFIED, &inst,
                           sizeof(inst)) != MEMTX_OK) {
        fprintf(stderr, "%s: Failed to read instruction\n", __func__);
        return false;
    }

    inst = le32_to_cpu(inst);

    bool success = true;
    switch (inst & 0x7FC00000) {
    case 0x29400000: { // ldp signed offset
        uint32_t dst1 = inst & 0x1F;
        uint64_t base = get_reg(cpu, (inst >> 5) & 0x1F);
        uint32_t dst2 = (inst >> 10) & 0x1F;
        uint32_t off = ((inst >> 15) & 0x7F);
        uint32_t reg_size = (inst & BIT(31)) == 0 ? 4 : 8;
        uint64_t addr = (off & BIT(6)) == 0 ?
                            base + (off * reg_size) :
                            base - (512 - (off & 0x3F) * reg_size);
        uint8_t data[0x10] = { 0 };
        if (address_space_read(
                cpu->as, arm_aarch64_fallback_emu_vtop(cpu, addr),
                MEMTXATTRS_UNSPECIFIED, data, reg_size * 2) == MEMTX_OK) {
            if (reg_size == sizeof(uint32_t)) {
                set_reg(cpu, dst1, ldl_le_p(data));
                set_reg(cpu, dst2, ldl_le_p(data + reg_size));
            } else {
                set_reg(cpu, dst1, ldq_le_p(data));
                set_reg(cpu, dst2, ldq_le_p(data + reg_size));
            }
        } else {
            success = false;
        }
        fprintf(stderr, "%s: LDP, success=%s\n", __func__,
                success ? "true" : "false");
        break;
    }
    case 0x29000000: { // stp signed offset
        uint64_t src1 = get_reg(cpu, inst & 0x1F);
        uint64_t base = get_reg(cpu, (inst >> 5) & 0x1F);
        uint64_t src2 = get_reg(cpu, (inst >> 10) & 0x1F);
        uint32_t off = ((inst >> 15) & 0x7F);
        uint32_t reg_size = (inst & BIT(31)) == 0 ? 4 : 8;
        uint64_t addr = (off & BIT(6)) == 0 ?
                            base + (off * reg_size) :
                            base - (512 - (off & 0x3F) * reg_size);
        uint8_t data[0x10] = { 0 };
        if (reg_size == sizeof(uint32_t)) {
            stl_le_p(data, src1 & 0xFFFFFFFF);
            stl_le_p(data + reg_size, src2 & 0xFFFFFFFF);
        } else {
            stq_le_p(data, src1);
            stq_le_p(data + reg_size, src2);
        }
        success = address_space_write(
                      cpu->as, arm_aarch64_fallback_emu_vtop(cpu, addr),
                      MEMTXATTRS_UNSPECIFIED, data, reg_size * 2) == MEMTX_OK;
        fprintf(stderr, "%s: STP, success=%s\n", __func__,
                success ? "true" : "false");
        break;
    }
    default:
        switch (inst & 0xBFC00000) {
        case 0xB8000000: { // str pre/post index
            if ((inst & BIT(10)) == 0) {
                fprintf(stderr, "%s: STR pre/post index does not match\n",
                        __func__);
                return false;
            }
            bool post = (inst & BIT(11)) == 0;
            uint64_t src = get_reg(cpu, inst & 0x1F);
            uint32_t base = (inst >> 5) & 0x1F;
            uint32_t reg_size = (inst & BIT(30)) == 0 ? 4 : 8;
            uint64_t off = ((inst >> 10) & 0xFFF) * reg_size;
            uint64_t addr = get_reg(cpu, base) + (post ? 0 : off);
            success = address_space_write(
                          cpu->as, arm_aarch64_fallback_emu_vtop(cpu, addr),
                          MEMTXATTRS_UNSPECIFIED, &src, reg_size) == MEMTX_OK;
            fprintf(stderr,
                    "%s: STR x%d, 0x%llX, 0x%llX, 0x%X, 0x%llX, success=%s\n",
                    __func__, base, src, off, reg_size, addr,
                    success ? "true" : "false");
            set_reg(cpu, base, addr + (post ? off : 0));
            break;
        }
        case 0xB9000000: { // str unsigned offset
            uint64_t src = get_reg(cpu, inst & 0x1F);
            uint64_t base = get_reg(cpu, (inst >> 5) & 0x1F);
            uint64_t off = ((inst >> 10) & 0xFFF);
            uint32_t reg_size = (inst & BIT(30)) == 0 ? 4 : 8;
            uint64_t addr = base + (off * reg_size);
            success = address_space_write(
                          cpu->as, arm_aarch64_fallback_emu_vtop(cpu, addr),
                          MEMTXATTRS_UNSPECIFIED, &src, reg_size) == MEMTX_OK;
            fprintf(
                stderr,
                "%s: STR 0x%llX, 0x%llX, 0x%llX, 0x%X, 0x%llX, success=%s\n",
                __func__, base, src, off, reg_size, addr,
                success ? "true" : "false");
            break;
        }
        case 0xB9400000: { // ldr unsigned offset
            uint32_t dst = inst & 0x1F;
            uint64_t base = get_reg(cpu, (inst >> 5) & 0x1F);
            uint64_t off = ((inst >> 10) & 0xFFF);
            uint32_t reg_size = (inst & BIT(30)) == 0 ? 4 : 8;
            uint64_t addr = base + (off * reg_size);
            uint32_t val = 0;
            if (address_space_read(
                    cpu->as, arm_aarch64_fallback_emu_vtop(cpu, addr),
                    MEMTXATTRS_UNSPECIFIED, &val, reg_size) == MEMTX_OK) {
                set_reg(cpu, dst, val);
            } else {
                success = false;
            }
            fprintf(stderr,
                    "%s: LDR x%d, 0x%llX, 0x%llX, 0x%X, 0x%llX, success=%s\n",
                    __func__, dst, base, off, reg_size, addr,
                    success ? "true" : "false");
            break;
        }
        case 0xB8400000: { // ldr pre/post index
            if ((inst & BIT(10)) == 0) {
                fprintf(stderr, "%s: LDR pre/post index does not match\n",
                        __func__);
                return false;
            }
            bool post = (inst & BIT(11)) == 0;
            uint32_t dst = inst & 0x1F;
            uint32_t base = (inst >> 5) & 0x1F;
            uint32_t reg_size = (inst & BIT(30)) == 0 ? 4 : 8;
            uint64_t off = ((inst >> 10) & 0xFFF) * reg_size;
            uint64_t addr = get_reg(cpu, base) + (post ? 0 : off);
            uint32_t val = 0;
            if (address_space_read(
                    cpu->as, arm_aarch64_fallback_emu_vtop(cpu, addr),
                    MEMTXATTRS_UNSPECIFIED, &val, reg_size) == MEMTX_OK) {
                set_reg(cpu, dst, val);
                set_reg(cpu, base, addr + (post ? off : 0));
            } else {
                success = false;
            }
            fprintf(stderr,
                    "%s: LDR x%d, x%d, 0x%llX, 0x%X, 0x%llX, success=%s\n",
                    __func__, dst, base, off, reg_size, addr,
                    success ? "true" : "false");
            break;
        }
        default:
            fprintf(stderr, "%s: inst(0x%X) does not match\n", __func__, inst);
            return false;
        }
        break;
    }

    return success;
}
