/*
 * Copyright (C) 2015, Ondrej Mosnacek <omosnacek@gmail.com>
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation: either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "passworddata.h"

#include "libpbkdf2-gpu-common/endianness.h"
#include "libpbkdf2-gpu-common/alignment.h"

#include <cstring>
#include <algorithm>

#define LUKS_PHDR_SIZE 592

#define LUKS_MAGIC_LENGTH 6
#define LUKS_KEY_ACTIVE   0x00AC71F3U
#define LUKS_KEY_DISABLED 0x0000DEADU

namespace lukscrack {

using namespace pbkdf2_gpu::common;

static const char *LUKS_MAGIC = "LUKS\xBA\xBE";

void PasswordData::readFromLuksHeader(std::istream &stream, std::size_t keyslot)
{
    if (keyslot > 8) {
        throw std::logic_error("Keyslot must be between 0-8!");
    }
    stream.exceptions(std::istream::eofbit | std::istream::failbit | std::istream::badbit);

    unsigned char buffer[LUKS_PHDR_SIZE];
    unsigned char *cursor = buffer;
    stream.read((char *)cursor, LUKS_PHDR_SIZE);
    if (std::memcmp(cursor, LUKS_MAGIC, LUKS_MAGIC_LENGTH) != 0) {
        throw InvalidLuksMagicException();
    }
    cursor += LUKS_MAGIC_LENGTH;

    std::size_t version = Endianness::read16BE(cursor);
    if (version > 1) {
        throw IncompatibleLuksVersionException(version);
    }
    cursor += 2;

    const char *cipherName = (const char *)cursor;
    if (std::find(cursor, cursor + 32, '\0') == cursor + 32) {
        throw StringNotTerminatedException("cipher-name");
    }
    cursor += 32;

    const char *cipherMode = (const char *)cursor;
    if (std::find(cursor, cursor + 32, '\0') == cursor + 32) {
        throw StringNotTerminatedException("cipher-mode");
    }
    cursor += 32;

    const char *hashSpec = (const char *)cursor;
    if (std::find(cursor, cursor + 32, '\0') == cursor + 32) {
        throw StringNotTerminatedException("hash-spec");
    }
    cursor += 32;

    cursor += 4; /* skip payloadOffset */

    std::size_t keySize = Endianness::read32BE(cursor);
    cursor += 4;

    const unsigned char *mkDigest = cursor;
    cursor += 20;

    const unsigned char *mkDigestSalt = cursor;
    cursor += 32;

    std::size_t mkDigestIter = Endianness::read32BE(cursor);
    cursor += 4;

    cursor += 40; /* skip the UUID */

    if (keyslot > 0) {
        /* jump to keyslot: */
        cursor += 48 * (keyslot - 1);

        std::uint_fast32_t active = Endianness::read32BE(cursor);
        if (active == LUKS_KEY_DISABLED) {
            throw KeyslotDisabledException(keyslot);
        } else if (active != LUKS_KEY_ACTIVE) {
            throw KeyslotCorruptedException(keyslot);
        }
        cursor += 4;
    } else {
        bool found = false;
        for (std::size_t i = 0; i < 8; i++) {
             std::uint_fast32_t active = Endianness::read32BE(cursor);
             if (active == LUKS_KEY_ACTIVE) {
                 cursor += 4;
                 found = true;
                 break;
             }
             cursor += 48;
        }
        if (!found) {
            throw NoActiveKeyslotException();
        }
    }

    std::size_t iter = Endianness::read32BE(cursor);
    cursor += 4;

    const unsigned char *keyslotSalt = cursor;
    cursor += 32;

    std::size_t keyMaterialOffset = Endianness::read32BE(cursor);
    cursor += 4;

    std::size_t stripes = Endianness::read32BE(cursor);
    cursor += 4;

    stream.seekg(keyMaterialOffset * SECTOR_SIZE, std::ios_base::beg);

    std::size_t keyMaterialSectors = BLOCK_COUNT(SECTOR_SIZE, keySize * stripes);
    std::size_t keyMaterialLength = keyMaterialSectors * SECTOR_SIZE;

    auto keyMaterial = std::unique_ptr<unsigned char[]>(new unsigned char[keyMaterialLength]);
    stream.read((char *)keyMaterial.get(), keyMaterialLength);

    this->hashSpec.assign(hashSpec);
    this->cipherName.assign(cipherName);
    this->cipherMode.assign(cipherMode);

    this->keySize = keySize;

    std::memcpy(this->masterKeyDigest, mkDigest, MASTER_KEY_DIGEST_LENGTH);
    std::memcpy(this->masterKeyDigestSalt, mkDigestSalt, SALT_LENGTH);
    this->masterKeyDigestIter = mkDigestIter;

    std::memcpy(this->keyslotSalt, keyslotSalt, SALT_LENGTH);
    this->keyslotIter = iter;
    this->keyslotStripes = stripes;

    this->keyMaterialSectors = keyMaterialSectors;
    this->keyMaterial = std::move(keyMaterial);
}

} // namespace lukscrack
