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

#include "sha1hashfunctionhelper.h"

#define IBLOCK_WORDS 16
#define OBLOCK_WORDS 5
#define ML_WORDS 2

#define ITERATIONS 80

namespace libpbkdf2 {
namespace compute {
namespace opencl {

static const char * const INIT_STATE[] = {
    "0x67452301",
    "0xEFCDAB89",
    "0x98BADCFE",
    "0x10325476",
    "0xC3D2E1F0",
};

static const char * const FS[] = {
    "SHA1_F0",
    "SHA1_F1",
    "SHA1_F2",
    "SHA1_F3",
};

static const char * const KS[] = {
    "0x5A827999",
    "0x6ED9EBA1",
    "0x8F1BBCDC",
    "0xCA62C1D6",
};

const Sha1HashFunctionHelper Sha1HashFunctionHelper::INSTANCE;

Sha1HashFunctionHelper::Sha1HashFunctionHelper()
    : HashFunctionHelper(
          false, "uint", 4, IBLOCK_WORDS, OBLOCK_WORDS, ML_WORDS, INIT_STATE)
{
}

void Sha1HashFunctionHelper::writeDefinitions(OpenCLWriter &out) const
{
    out << "#ifdef cl_nv_pragma_unroll" << std::endl;
    out << "#define NVIDIA" << std::endl;
    out << "#endif /* cl_nv_pragma_unroll */" << std::endl;
    out << std::endl;
    out << "#ifndef NVIDIA" << std::endl;
    out << "#define SHA_F0(x,y,z) bitselect(z, y, x)" << std::endl;
    out << "#else" << std::endl;
    out << "#define SHA_F0(x,y,z) (z ^ (x & (y ^ z)))" << std::endl;
    out << "#endif /* NVIDIA */" << std::endl;
    out << std::endl;
    out << "#define SHA_F1(x,y,z) bitselect(y, x, y ^ z)" << std::endl;
    out << std::endl;
    out << "#define SHA1_F0(x, y, z) SHA_F0(x, y, z)" << std::endl;
    out << "#define SHA1_F1(x, y, z) (x ^ y ^ z)" << std::endl;
    out << "#define SHA1_F2(x, y, z) SHA_F1(x, y, z)" << std::endl;
    out << "#define SHA1_F3(x, y, z) (x ^ y ^ z)" << std::endl;
    out << std::endl;
}

void Sha1HashFunctionHelper::writeUpdate(
        OpenCLWriter &writer,
        const std::vector<std::string> &prevState,
        const std::vector<std::string> &state,
        const std::vector<std::string> &inputBlock,
        bool swap) const
{
    for (std::size_t i = 0; i < OBLOCK_WORDS; i++) {
        writer.beginAssignment(state[i]);
        writer << prevState[i];
        writer.endAssignment();
    }
    writer.writeEmptyLine();

    const std::vector<std::string> &dest = swap ? prevState : state;
    const std::vector<std::string> &aux  = swap ? state : prevState;

    for (std::size_t iter = 0; iter < ITERATIONS; iter++) {
        std::size_t state_a = (ITERATIONS - iter) % OBLOCK_WORDS;
        std::size_t quarter = iter / (ITERATIONS / 4);

        writer.beginAssignment(dest[(state_a + 4) % OBLOCK_WORDS]);
        writer << "rotate(" << dest[(state_a + 0) % OBLOCK_WORDS]
               << ", (uint)5) + " << FS[quarter]
               << "(" << dest[(state_a + 1) % OBLOCK_WORDS]
               << ", " << dest[(state_a + 2) % OBLOCK_WORDS]
               << ", " << dest[(state_a + 3) % OBLOCK_WORDS]
               << ") + " << KS[quarter] << " + "
               << dest[(state_a + 4) % OBLOCK_WORDS] << " + "
               << inputBlock[iter % IBLOCK_WORDS];
        writer.endAssignment();

        writer.beginAssignment(dest[(state_a + 1) % OBLOCK_WORDS]);
        writer << "rotate(" << dest[(state_a + 1) % OBLOCK_WORDS]
               << ", (uint)30)";
        writer.endAssignment();

        if (iter % IBLOCK_WORDS == IBLOCK_WORDS - 1) {
            writer.writeEmptyLine();
            for (std::size_t i = 0; i < IBLOCK_WORDS; i++) {
                writer.beginAssignment(inputBlock[i]);
                writer << "rotate("
                       << inputBlock[(i + 13) % IBLOCK_WORDS] << " ^ "
                       << inputBlock[(i +  8) % IBLOCK_WORDS] << " ^ "
                       << inputBlock[(i +  2) % IBLOCK_WORDS] << " ^ "
                       << inputBlock[(i +  0) % IBLOCK_WORDS]
                       << ", (uint)1)";
                writer.endAssignment();
            }
        }
        writer.writeEmptyLine();
    }

    for (std::size_t i = 0; i < OBLOCK_WORDS; i++) {
        writer.beginAssignment(dest[i]);
        writer << dest[i] << " + " << aux[i];
        writer.endAssignment();
    }
}

} // namespace opencl
} // namespace compute
} // namespace libpbkdf2

