#include <vector>
#include "fasta.hpp"
#include "exceptions.hpp"

uint64_t read_references(std::vector<std::string> &seqs, std::vector<unsigned int> &lengths, idx_to_acc &acc_map, std::string fn)
{
    uint64_t total_ref_seq_size = 0;
    std::ifstream file(fn);
    std::string line, seq;
    acc_map.clear();

    if (!file.good()) {
        throw InvalidFasta("Cannot read from FASTA file");
    }

    auto c = file.peek();
    if (c != '>') {
        std::ostringstream oss;
        oss << "FASTA file must begin with '>' character, not '"
            << static_cast<unsigned char>(c) << "'";
        throw InvalidFasta(oss.str().c_str());
    }

    while (getline(file, line)) {
        if (line[0] == '>') {
//            std::cerr << ref_index << " " << line << std::endl;
            if (seq.length() > 0) {
//                seqs[ref_index -1] = seq;
                seqs.push_back(seq);
                lengths.push_back(seq.length());
                total_ref_seq_size += seq.length();
//                std::cerr << ref_index - 1 << " here " << seq << " " << seq.length() << " " << seq.size() << std::endl;
//                generate_kmers(h, k, seq, ref_index);
            }
//            acc_map.push_back(line.substr(1, line.length() -1); //line;
            acc_map.push_back(line.substr(1, line.find(' ') -1)); // cutting at first space;
            seq = "";
        }
        else {
            seq += line;
        }
    }
    if (seq.length() > 0){
        seqs.push_back(seq);
        lengths.push_back(seq.length());
        total_ref_seq_size += seq.length();
    }

    return total_ref_seq_size;
}


References References::from_fasta(std::string path) {
    std::vector<std::string> sequences;
    std::vector<std::string> names;
    std::vector<unsigned int> lengths;
    read_references(sequences, lengths, names, path);
    return References(std::move(sequences), std::move(names), std::move(lengths));
}
