//---------------------------------------------------------------------------//
// Copyright (c) 2021 Mikhail Komarov <nemo@nil.foundation>
// Copyright (c) 2021 Nikita Kaskov <nbering@nil.foundation>
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//---------------------------------------------------------------------------//

#include <cstdint>
#include <cstdio>
#include <fstream>

#include <nil/crypto3/algebra/curves/pallas.hpp>

#include <nil/crypto3/zk/snark/arithmetization/plonk/params.hpp>
#include <nil/crypto3/zk/snark/arithmetization/plonk/constraint_system.hpp>

#include <ios>

#include <nil/marshalling/status_type.hpp>
#include <nil/marshalling/field_type.hpp>
#include <nil/marshalling/endianness.hpp>
#include <nil/crypto3/marshalling/zk/types/plonk/constraint_system.hpp>

#include <nil/blueprint/parser.hpp>
#include <nil/blueprint/utils/table_profiling.hpp>
#include <nil/blueprint/utils/satisfiability_check.hpp>

using namespace nil;
using namespace nil::crypto3;

template<typename TIter>
void print_hex_byteblob(std::ostream &os, TIter iter_begin, TIter iter_end, bool endl) {
    os << "0x" << std::hex;
    for (TIter it = iter_begin; it != iter_end; it++) {
        os << std::setfill('0') << std::setw(2) << std::right << int(*it);
    }
    os << std::dec;
    if (endl) {
        os << std::endl;
    }
}

template<typename Endianness, typename ConstraintSystemType>
void print_circuit(const ConstraintSystemType &circuit, std::ostream &out = std::cout) {

    using TTypeBase = nil::marshalling::field_type<Endianness>;
    auto filled_val =
        nil::crypto3::marshalling::types::fill_plonk_constraint_system<ConstraintSystemType, Endianness>(circuit);

    std::vector<std::uint8_t> cv;
    cv.resize(filled_val.length(), 0x00);
    auto write_iter = cv.begin();
    nil::marshalling::status_type status = filled_val.write(write_iter, cv.size());
    print_hex_byteblob(out, cv.cbegin(), cv.cend(), false);
}

int main(int argc, char *argv[]) {
    if (argc != 2 && argc != 4 && argc != 3 && argc != 5) {
        std::cerr << "Usage: " << argv[0] << "[-i input_file] ir_file output_folder_name" << std::endl;
        return 1;
    }
    std::string input_file_name = "input.txt";
    char *ir_file = argv[1];
    std::string output_folder_name = ".";

    if (argc == 3) output_folder_name = argv[2];
    if (argc == 5) output_folder_name = argv[4];

    if (argc == 4 || argc == 5) {
        if (argv[1] == std::string("-i")) {
            input_file_name = argv[2];
            ir_file = argv[3];
        } else if (argv[2] == std::string("-i")) {
            input_file_name = argv[3];
            ir_file = argv[1];
        } else {
            std::cerr << "Usage: " << argv[0] << "[-i input_file] ir_file" << std::endl;
            return 1;
        }
    }

    using curve_type = algebra::curves::pallas;
    using BlueprintFieldType = typename curve_type::base_field_type;
    constexpr std::size_t WitnessColumns = 15;
    constexpr std::size_t PublicInputColumns = 5;
    constexpr std::size_t ConstantColumns = 5;
    constexpr std::size_t SelectorColumns = 20;

    using ArithmetizationParams =
        zk::snark::plonk_arithmetization_params<WitnessColumns, PublicInputColumns, ConstantColumns, SelectorColumns>;
    using ConstraintSystemType = zk::snark::plonk_constraint_system<BlueprintFieldType, ArithmetizationParams>;

    std::vector<typename BlueprintFieldType::value_type> public_input;
    long long number;
    auto fptr = std::fopen(input_file_name.c_str(), "r");
    if (fptr == NULL) {
        std::cerr << "Could not open the file - '" << input_file_name << "'" << std::endl;
        return EXIT_FAILURE;
    }

    while (!std::feof(fptr)) {
        fscanf(fptr, "%lld\n", &number);
        public_input.push_back(number);
    }
    nil::blueprint::parser<BlueprintFieldType, ArithmetizationParams> parser_instance;

    std::unique_ptr<llvm::Module> module = parser_instance.parseIRFile(ir_file);
    if (module == nullptr) {
        return 1;
    }

    if (!parser_instance.evaluate(*module, public_input)) {
        return 1;
    }

    zk::snark::plonk_table_description<BlueprintFieldType, ArithmetizationParams> desc;
    desc.usable_rows_amount = parser_instance.assignmnt.rows_amount();
    desc.rows_amount = zk::snark::basic_padding(parser_instance.assignmnt);

    std::ofstream otable;
    otable.open(output_folder_name+"/assignment_table.data");
    if( !otable ){
        std::cout << "Something wrong with output " << output_folder_name+"/assignment_table.data" << std::endl;
        return 1;
    }
    nil::blueprint::profiling_assignment_table(parser_instance.assignmnt, desc.usable_rows_amount, otable);
    otable.close();

    std::ofstream ocircuit;
    ocircuit.open(output_folder_name+"/circuit.bin");
    if( !ocircuit ){
        std::cout << "Something wrong with output " << output_folder_name+"/circuit.bin" << std::endl;
        return 1;
    }
    print_circuit<nil::marshalling::option::big_endian, ConstraintSystemType>(parser_instance.bp, ocircuit);
    ocircuit.close();

    return !nil::blueprint::is_satisfied(parser_instance.bp, parser_instance.assignmnt);
}
