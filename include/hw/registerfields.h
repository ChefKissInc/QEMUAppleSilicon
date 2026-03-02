/*
 * Register Definition Macros.
 *
 * Copyright (c) 2026 Visual Ehrmanntraut (VisualEhrmanntraut).
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

#ifndef HW_REGISTERFIELDS_H
#define HW_REGISTERFIELDS_H

#include "qemu/bitops.h"

#define REG8(_reg, _addr)        \
    enum { A_##_reg = (_addr) }; \
    enum { R_##_reg = (_addr) };
#define REG16(_reg, _addr)       \
    enum { A_##_reg = (_addr) }; \
    enum { R_##_reg = (_addr) / 2 };
#define REG32(_reg, _addr)       \
    enum { A_##_reg = (_addr) }; \
    enum { R_##_reg = (_addr) / 4 };
#define REG64(_reg, _addr)       \
    enum { A_##_reg = (_addr) }; \
    enum { R_##_reg = (_addr) / 8 };

#define REG_FIELD(_reg, _field, _shift, _length)       \
    enum { R_##_reg##_##_field##_SHIFT = (_shift) };   \
    enum { R_##_reg##_##_field##_LENGTH = (_length) }; \
    enum { R_##_reg##_##_field##_MASK = MAKE_64BIT_MASK(_shift, _length) };

#define REG_FIELD_EX8(_storage, _reg, _field)         \
    extract8((_storage), R_##_reg##_##_field##_SHIFT, \
             R_##_reg##_##_field##_LENGTH)
#define REG_FIELD_EX16(_storage, _reg, _field)         \
    extract16((_storage), R_##_reg##_##_field##_SHIFT, \
              R_##_reg##_##_field##_LENGTH)
#define REG_FIELD_EX32(_storage, _reg, _field)         \
    extract32((_storage), R_##_reg##_##_field##_SHIFT, \
              R_##_reg##_##_field##_LENGTH)
#define REG_FIELD_EX64(_storage, _reg, _field)         \
    extract64((_storage), R_##_reg##_##_field##_SHIFT, \
              R_##_reg##_##_field##_LENGTH)

#define REG_FIELD_SEX8(_storage, _reg, _field)         \
    sextract8((_storage), R_##_reg##_##_field##_SHIFT, \
              R_##_reg##_##_field##_LENGTH)
#define REG_FIELD_SEX16(_storage, _reg, _field)         \
    sextract16((_storage), R_##_reg##_##_field##_SHIFT, \
               R_##_reg##_##_field##_LENGTH)
#define REG_FIELD_SEX32(_storage, _reg, _field)         \
    sextract32((_storage), R_##_reg##_##_field##_SHIFT, \
               R_##_reg##_##_field##_LENGTH)
#define REG_FIELD_SEX64(_storage, _reg, _field)         \
    sextract64((_storage), R_##_reg##_##_field##_SHIFT, \
               R_##_reg##_##_field##_LENGTH)

#define REG_ARRAY_FIELD_EX32(_regs, _reg, _field) \
    REG_FIELD_EX32((_regs)[R_##_reg], _reg, _field)
#define REG_ARRAY_FIELD_EX64(_regs, _reg, _field) \
    REG_FIELD_EX64((_regs)[R_##_reg], _reg, _field)

#define REG_FIELD_DP8(_storage, _reg, _field, _val)             \
    ({                                                          \
        struct {                                                \
            unsigned int v : R_##_reg##_##_field##_LENGTH;      \
        } _v = { _val };                                        \
        uint8_t _d;                                             \
        _d = deposit32((_storage), R_##_reg##_##_field##_SHIFT, \
                       R_##_reg##_##_field##_LENGTH, _v.v);     \
        _d;                                                     \
    })
#define REG_FIELD_DP16(_storage, _reg, _field, _val)            \
    ({                                                          \
        struct {                                                \
            unsigned int v : R_##_reg##_##_field##_LENGTH;      \
        } _v = { _val };                                        \
        uint16_t _d;                                            \
        _d = deposit32((_storage), R_##_reg##_##_field##_SHIFT, \
                       R_##_reg##_##_field##_LENGTH, _v.v);     \
        _d;                                                     \
    })
#define REG_FIELD_DP32(_storage, _reg, _field, _val)            \
    ({                                                          \
        struct {                                                \
            unsigned int v : R_##_reg##_##_field##_LENGTH;      \
        } _v = { _val };                                        \
        uint32_t _d;                                            \
        _d = deposit32((_storage), R_##_reg##_##_field##_SHIFT, \
                       R_##_reg##_##_field##_LENGTH, _v.v);     \
        _d;                                                     \
    })
#define REG_FIELD_DP64(_storage, _reg, _field, _val)            \
    ({                                                          \
        struct {                                                \
            uint64_t v : R_##_reg##_##_field##_LENGTH;          \
        } _v = { _val };                                        \
        uint64_t _d;                                            \
        _d = deposit64((_storage), R_##_reg##_##_field##_SHIFT, \
                       R_##_reg##_##_field##_LENGTH, _v.v);     \
        _d;                                                     \
    })

#define REG_FIELD_SDP8(_storage, _reg, _field, _val)            \
    ({                                                          \
        struct {                                                \
            signed int v : R_##_reg##_##_field##_LENGTH;        \
        } _v = { _val };                                        \
        uint8_t _d;                                             \
        _d = deposit32((_storage), R_##_reg##_##_field##_SHIFT, \
                       R_##_reg##_##_field##_LENGTH, _v.v);     \
        _d;                                                     \
    })
#define REG_FIELD_SDP16(_storage, _reg, _field, _val)           \
    ({                                                          \
        struct {                                                \
            signed int v : R_##_reg##_##_field##_LENGTH;        \
        } _v = { _val };                                        \
        uint16_t _d;                                            \
        _d = deposit32((_storage), R_##_reg##_##_field##_SHIFT, \
                       R_##_reg##_##_field##_LENGTH, _v.v);     \
        _d;                                                     \
    })
#define REG_FIELD_SDP32(_storage, _reg, _field, _val)           \
    ({                                                          \
        struct {                                                \
            signed int v : R_##_reg##_##_field##_LENGTH;        \
        } _v = { _val };                                        \
        uint32_t _d;                                            \
        _d = deposit32((_storage), R_##_reg##_##_field##_SHIFT, \
                       R_##_reg##_##_field##_LENGTH, _v.v);     \
        _d;                                                     \
    })
#define REG_FIELD_SDP64(_storage, _reg, _field, _val)           \
    ({                                                          \
        struct {                                                \
            int64_t v : R_##_reg##_##_field##_LENGTH;           \
        } _v = { _val };                                        \
        uint64_t _d;                                            \
        _d = deposit64((_storage), R_##_reg##_##_field##_SHIFT, \
                       R_##_reg##_##_field##_LENGTH, _v.v);     \
        _d;                                                     \
    })

#define REG_ARRAY_FIELD_DP32(_regs, _reg, _field, _val) \
    (_regs)[R_##_reg] = REG_FIELD_DP32((_regs)[R_##_reg], _reg, _field, _val);
#define REG_ARRAY_FIELD_DP64(_regs, _reg, _field, _val) \
    (_regs)[R_##_reg] = REG_FIELD_DP64((_regs)[R_##_reg], _reg, _field, _val);

#define SHARED_REG_FIELD(_name, _shift, _length) \
    enum { _name##_##SHIFT = (_shift) };         \
    enum { _name##_##LENGTH = (_length) };       \
    enum { _name##_##MASK = MAKE_64BIT_MASK(_shift, _length) };

#define SHARED_REG_FIELD_EX8(_storage, _field) \
    extract8((_storage), _field##_SHIFT, _field##_LENGTH)
#define SHARED_REG_FIELD_EX16(_storage, _field) \
    extract16((_storage), _field##_SHIFT, _field##_LENGTH)
#define SHARED_REG_FIELD_EX32(_storage, _field) \
    extract32((_storage), _field##_SHIFT, _field##_LENGTH)
#define SHARED_REG_FIELD_EX64(_storage, _field) \
    extract64((_storage), _field##_SHIFT, _field##_LENGTH)

#define SHARED_REG_ARRAY_FIELD_EX32(_regs, _offset, _field) \
    SHARED_REG_FIELD_EX32((_regs)[(_offset)], _field)
#define SHARED_REG_ARRAY_FIELD_EX64(_regs, _offset, _field) \
    SHARED_REG_FIELD_EX64((_regs)[(_offset)], _field)

#define SHARED_REG_FIELD_DP8(_storage, _field, _val)                       \
    ({                                                                     \
        struct {                                                           \
            unsigned int v : _field##_LENGTH;                              \
        } _v = { _val };                                                   \
        uint8_t _d;                                                        \
        _d = deposit32((_storage), _field##_SHIFT, _field##_LENGTH, _v.v); \
        _d;                                                                \
    })
#define SHARED_REG_FIELD_DP16(_storage, _field, _val)                      \
    ({                                                                     \
        struct {                                                           \
            unsigned int v : _field##_LENGTH;                              \
        } _v = { _val };                                                   \
        uint16_t _d;                                                       \
        _d = deposit32((_storage), _field##_SHIFT, _field##_LENGTH, _v.v); \
        _d;                                                                \
    })
#define SHARED_REG_FIELD_DP32(_storage, _field, _val)                      \
    ({                                                                     \
        struct {                                                           \
            unsigned int v : _field##_LENGTH;                              \
        } _v = { _val };                                                   \
        uint32_t _d;                                                       \
        _d = deposit32((_storage), _field##_SHIFT, _field##_LENGTH, _v.v); \
        _d;                                                                \
    })
#define SHARED_REG_FIELD_DP64(_storage, _field, _val)                      \
    ({                                                                     \
        struct {                                                           \
            uint64_t v : _field##_LENGTH;                                  \
        } _v = { _val };                                                   \
        uint64_t _d;                                                       \
        _d = deposit64((_storage), _field##_SHIFT, _field##_LENGTH, _v.v); \
        _d;                                                                \
    })

#define SHARED_REG_ARRAY_FIELD_DP32(_regs, _offset, _field, _val) \
    (_regs)[(_offset)] =                                          \
        SHARED_REG_FIELD_DP32((_regs)[(_offset)], _field, _val);
#define SHARED_REG_ARRAY_FIELD_DP64(_regs, _offset, _field, _val) \
    (_regs)[(_offset)] =                                          \
        SHARED_REG_FIELD_DP64((_regs)[(_offset)], _field, _val);

#endif /* HW_REGISTERFIELDS_H */
