#include <iostream>
#include <fstream>
#include <unordered_map>
#include "source/robin_hood.h"
#include <vector>
#include <string>
#include <chrono>  // for high_resolution_clock

#include "source/edlib.h"
#include "source/index.hpp"
//#include "gap_affine/affine_wavefront_align.h"
#include "source/ksw2.h"

//develop
#include <chrono>
#include <thread>


//typedef robin_hood::unordered_map< unsigned int , std::string > references;
typedef robin_hood::unordered_map< unsigned int, std::string > idx_to_acc;

typedef robin_hood::unordered_map< uint64_t, std::tuple<uint64_t, unsigned int >> vector_index;

typedef std::vector< std::tuple<uint64_t, unsigned int, unsigned int>> mers_vector;



static void read_references(std::vector<std::string> &seqs, std::vector<unsigned int> &lengths, idx_to_acc &acc_map, std::string fn)
{
    std::ifstream file(fn);
    std::string line, seq;
    int ref_index = 0;
    while (getline(file, line)) {
        if (line[0] == '>') {
//            std::cout << ref_index << " " << line << std::endl;
            if (seq.length() > 0){
//                seqs[ref_index -1] = seq;
                seqs.push_back(seq);
                lengths.push_back(seq.length());
//                std::cout << ref_index - 1 << " here " << seq << " " << seq.length() << " " << seq.size() << std::endl;
//                generate_kmers(h, k, seq, ref_index);
            }
            acc_map[ref_index] = line.substr(1, line.length() -1); //line;
            ref_index++;
            seq = "";
        }
        else {
            seq += line;
        }
    }
    if (seq.length() > 0){
//        seqs[ref_index -1] = seq;
        seqs.push_back(seq);
        lengths.push_back(seq.length());
//        std::cout << ref_index -1 << " here2 " << seq << std::endl;
//        generate_kmers(h, k, seq, ref_index);
    }
    file.close();
}



static inline void print_diagnostics_new4(mers_vector_reduced &mers_vector, kmer_lookup mers_index ) {
    uint64_t tot_flat_vector_size = 0;
    for (size_t i = 0; i < mers_vector.size(); ++i)
    {
        // access using []
        auto t = mers_vector[i];
//        std::cout << "(" << std::get<0>(t) << ", " << std::get<1>(t) << ", " << std::get<2>(t) << ", " << std::get<3>(t) << ", " << std::get<4>(t) << "), ";
        tot_flat_vector_size += sizeof(t);
    }
    std::cout << "Total size of flat mers-vector : " << tot_flat_vector_size/1000000  << " Mb." << std::endl;
    std::cout << "Total entries in flat mers-vector : " << mers_vector.size()  << std::endl;
//    uint64_t tot_hashtable_index_size = 0;
//    for (auto &it : mers_index)
//    {
////        std::cout << it.first << ": (" << std::get<0>(it.second) << ", " << std::get<1>(it.second) << "), " ;
//        tot_hashtable_index_size += sizeof(it.first);
//        tot_hashtable_index_size += sizeof(it.second);
//    }
//    std::cout << "Total size of hash table index : " << tot_hashtable_index_size/1000000  << " Mb." << std::endl;

    std::cout << "Total entries in hash table : " << mers_index.size()  << std::endl;
    std::cout << "Total size of hash table entries (ignoring internal nodes) : " << (mers_index.size() * sizeof(kmer_lookup::value_type))/1000000 << " Mb." << "\n";
    // https://stackoverflow.com/questions/720507/how-can-i-estimate-memory-usage-of-stdmap/720520
    std::cout << "Total size of hash table (applying approximation that about 2/3 of the memory is from internal nodes and 1/3 from the leafs, i.e., hash entries) : " << 3*(mers_index.size() * sizeof(kmer_lookup::value_type))/1000000 << " Mb." << "\n";
    std::cout << "" << std::endl;
    std::cout << "Total index size: " <<  tot_flat_vector_size/1000000 +  3*(mers_index.size() * sizeof(kmer_lookup::value_type))/1000000 << " Mb." << std::endl;

}





static inline std::vector<nam> find_nams(mers_vector &query_mers, mers_vector_reduced &mers_vector, kmer_lookup &mers_index, int k, std::vector<std::string> &ref_seqs, std::string &read, bool is_rc, unsigned int hit_upper_window_lim){
//    std::cout << "ENTER FIND NAMS " <<  std::endl;
    robin_hood::unordered_map< unsigned int, std::vector<hit>> hits_per_ref; // [ref_id] -> vector( struct hit)
    uint64_t hit_count_reduced = 0;
    uint64_t hit_count_all = 0;
    int read_length = read.length();
//    std::cout << " "  <<  std::endl;
    for (auto &q : query_mers)
//    for (size_t i = 0; i < query_mers.size(); ++i)
    {
        hit h;
        h.query_s = std::get<2>(q);
        h.query_e = h.query_s + read_length/2;
//        std::cout << "Q " << h.query_s << " " << h.query_e << " read length:" << read_length << std::endl;
        uint64_t mer_hashv = std::get<0>(q);
        if (mers_index.find(mer_hashv) != mers_index.end()){ //  In  index
            std::tuple<uint64_t, unsigned int> mer;
            mer = mers_index[mer_hashv];
            uint64_t offset = std::get<0>(mer);
            unsigned int count = std::get<1>(mer);
            for(size_t j = offset; j < offset+count; ++j)
            {
                auto r = mers_vector[j];
                unsigned int ref_s = std::get<1>(r);
                unsigned int ref_e = ref_s + read_length/2;
                unsigned int ref_id = std::get<0>(r);

                h.ref_s = ref_s;
                h.ref_e = ref_e;
                hits_per_ref[ref_id].push_back(h);
//                std::cout << "Found: " <<  h.query_s << " " << h.query_e << " ref: " <<  h.ref_s << " " << h.ref_e <<  std::endl;
                hit_count_all ++;

            }

        }
    }

//    std::cout << "NUMBER OF HITS GENERATED: " << hit_count_all << std::endl;
    std::vector<nam> open_nams;
    std::vector<nam> final_nams; // [ref_id] -> vector(struct nam)

    for (auto &it : hits_per_ref)
    {
        unsigned int ref_id = it.first;
        std::vector<hit> hits = it.second;
        open_nams = std::vector<nam> (); // Initialize vector
        uint64_t prev_q_start = 0;
        for (auto &h : hits){
            bool is_added = false;
//            std::cout << "HIT " << h.query_s <<  ", " << h.query_e << ", " << h.ref_s <<  ", " << h.ref_e << std::endl;
            for (auto & o : open_nams) {

                // Extend NAM
                if ( ( o.previous_query_start <= h.query_s) && (h.query_s <= o.query_e ) && ( o.previous_ref_start <= h.ref_s) && (h.ref_s <= o.ref_e) ){
                    if (h.query_e > o.query_e) {
                        o.query_e = h.query_e;
                    }
                    if (h.ref_e > o.ref_e) {
                        o.ref_e = h.ref_e;
                    }
                    o.previous_query_start = h.query_s;
                    o.previous_ref_start = h.ref_s; // keeping track so that we don't . Can be caused by interleaved repeats.
                    o.query_last_hit_pos = h.query_s; // log the last strobemer hit in case of outputting paf
                    o.ref_last_hit_pos = h.ref_s; // log the last strobemer hit in case of outputting paf
                    o.n_hits ++;
                    is_added = true;
                    break;
                }


            }

            // Add the hit to open matches
            if (not is_added){
                nam n;
                n.query_s = h.query_s;
                n.query_e = h.query_e;
                n.ref_s = h.ref_s;
                n.ref_e = h.ref_e;
                n.ref_id = ref_id;
                n.previous_query_start = h.query_s;
                n.previous_ref_start = h.ref_s;
                n.query_last_hit_pos = h.query_s;
                n.ref_last_hit_pos = h.ref_s;
                n.n_hits = 1;
                n.is_rc = is_rc;
                open_nams.push_back(n);
            }


            // Only filter if we have advanced at least k nucleotides
            if (h.query_s > prev_q_start + k) {

                // Output all NAMs from open_matches to final_nams that the current hit have passed
                for (auto &n : open_nams) {
                    if (n.query_e < h.query_s) {
                        final_nams.push_back(n);
                    }
                }

                // Remove all NAMs from open_matches that the current hit have passed
                unsigned int c = h.query_s;
                auto predicate = [c](decltype(open_nams)::value_type const &nam) { return nam.query_e < c; };
                open_nams.erase(std::remove_if(open_nams.begin(), open_nams.end(), predicate), open_nams.end());
                prev_q_start = h.query_s;
            }


        }

        // Add all current open_matches to final NAMs
        for (auto &n : open_nams){
            final_nams.push_back(n);
        }
    }

//    for (auto &n : final_nams){
//        std::cout << n.ref_id << ": (" << n.query_s << ", " << n.query_e << ", " << n.ref_s << ", " << n.ref_e << ")" << std::endl;
//    }


    return final_nams;
}




static inline std::vector<hit> find_hits(mers_vector &query_mers, mers_vector_reduced &mers_vector, vector_index &mers_index, int k, std::vector<std::string> &ref_seqs, std::string &read){
//    std::cout << "ENTER FIND NAMS " <<  std::endl;
    std::vector<hit> query_hits;
    int extra = 20;
    uint64_t hit_count_reduced = 0;
    uint64_t hit_count_all = 0;
    int read_length = read.length();
    for (auto &q : query_mers)
//    for (size_t i = 0; i < query_mers.size(); ++i)
    {
        hit h;
        h.query_s = std::get<2>(q);
//        std::cout << h.query_s << " " << h.query_e <<  std::endl;

        uint64_t mer_hashv = std::get<0>(q);
        if (mers_index.find(mer_hashv) != mers_index.end()){ //  In  index
            std::tuple<uint64_t, unsigned int> mer;
            mer = mers_index[mer_hashv];
            uint64_t offset = std::get<0>(mer);
            unsigned int count = std::get<1>(mer);
            for(size_t j = offset; j < offset+count; ++j)
            {

                auto r = mers_vector[j];
                unsigned int ref_s = std::get<1>(r);
                unsigned int ref_id = std::get<0>(r);

                h.ref_s = ref_s;
                h.ref_id = ref_id;
                query_hits.push_back(h);

                hit_count_all ++;
                std::string ref_segm;
//                ref_segm = ref_seqs[ref_id].substr(ref_s - h.query_s, read_length);

//                EdlibAlignResult result = edlibAlign("hello", 5, "world!", 6, edlibDefaultAlignConfig());
//                if (result.status == EDLIB_STATUS_OK) {
//                    printf("edit_distance('hello', 'world!') = %d\n", result.editDistance);
//                    printf("edit_distance('hello', 'world!') = %d\n", result2.editDistance);
//                }

//                EdlibAlignResult result2 = edlibAlign(&read[0], read_length, &ref_segm[0], read_length, edlibNewAlignConfig(60, EDLIB_MODE_HW, EDLIB_TASK_PATH, NULL, 0));
//                char* cigar = edlibAlignmentToCigar(result2.alignment, result2.alignmentLength, EDLIB_CIGAR_STANDARD);
////                std::cout <<  cigar << std::endl;
//                edlibFreeAlignResult(result2);
//                free(cigar);

//                std::cout << "Hit! " << h.query_s << ", " << h.query_e << ", " << ref_s << ", " << ref_e << ", " << std::endl;

            }

        }
    }


    return query_hits;
}


static inline bool compareByQueryCoord(const hit &a, const hit &b)
{
    // first sort on ref ID, then on query, then on reference
    return (a.ref_id < b.ref_id) ||
           ( (a.ref_id == b.ref_id) && (a.query_s < b.query_s) ) ||
           ((a.ref_id == b.ref_id) && (a.query_s == b.query_s ) && (a.ref_s < b.ref_s)) ;
}

static inline bool compareByQueryLength(const hit &a, const hit &b)
{
    return (a.query_e - a.query_s) < ( b.query_e - b.query_s);
}

static inline bool compareByNrHitsAndSimilarSpan(const nam &a, const nam &b)
{
    // first sort on nr hits, then on diff in span between query and reference, then on reference
    return (a.n_hits > b.n_hits) ||
           ( (a.n_hits == b.n_hits) && ( ((a.query_e - a.query_s) - (a.ref_e - a.ref_s)) < ((b.query_e - b.query_s) - (b.ref_e - b.ref_s)) ) );
}

static inline void output_hits(std::vector<nam> &nams, std::ofstream &output_file, std::string query_acc, idx_to_acc &acc_map) {
    //Sort hits based on start choordinate on query sequence
//    std::sort(nams.begin(), nams.end(), compareByQueryCoord);
    // Output results
    output_file << "> " << query_acc << "\n";
    for (auto &n : nams) {
        output_file << "  " << acc_map[n.ref_id]  << " " << n.ref_s << " " << n.query_s << " -" << "\n";
//      python: outfile.write("  {0} {1} {2} {3}\n".format(ref_acc, ref_p, q_pos, k))
    }
}

static inline void output_hits_paf(std::vector<nam> &nams, std::vector<nam> &nams_rc, std::ofstream &output_file, std::string query_acc, idx_to_acc &acc_map, int k, int read_len, std::vector<unsigned int> &ref_len_map) {
    // Merge fwd and reverse complement hits
    std::vector<nam> all_nams;
    all_nams.reserve( nams.size() + nams_rc.size() ); // preallocate memory
    all_nams.insert( all_nams.end(), nams.begin(), nams.end() );
    all_nams.insert( all_nams.end(), nams_rc.begin(), nams_rc.end() );

    //Sort hits based on start choordinate on query sequence
    std::sort(all_nams.begin(), all_nams.end(), compareByNrHitsAndSimilarSpan);

    // Output results
//    int cnt = 0;
    // Only output single best hit based on: Firstly: number of randstrobe-hits. Secondly the concordance the span of the hits between ref and query (more simmilar ranked higher)
    for (auto &n : all_nams) {
//        if (cnt > 1){ // only output top 2 hits
//            break;
//        }
        std::string o;
        if (n.is_rc){
            o = "-";
        }
        else{
            o = "+";
        }
        output_file << query_acc << "\t" << read_len <<  "\t" << n.query_s << "\t" << n.query_last_hit_pos + k << "\t" << o  <<  "\t" << acc_map[n.ref_id] << "\t" << ref_len_map[n.ref_id] << "\t" << n.ref_s << "\t" << n.ref_last_hit_pos + k << "\t" << n.n_hits << "\t" << n.ref_last_hit_pos + k - n.ref_s << "\t" << "-" << "\n";
//        break;
//        cnt ++;

        //        output_file << "  " << acc_map[n.ref_id]  << " " << n.ref_s << " " << n.query_s << " -" << "\n";
//      python: outfile.write("  {0} {1} {2} {3}\n".format(ref_acc, ref_p, q_pos, k))
    }
}

static inline std::string reverse_complement(std::string &read) {
    auto read_rev = read;
    std::reverse(read_rev.begin(), read_rev.end()); // reverse
//    std::cout << read_rev << std::endl;
    for (size_t j = 0; j < read_rev.length(); ++j) { // complement
        if (read_rev[j] == 'A') read_rev[j] = 'T';
        else if (read_rev[j] == 'T') read_rev[j] = 'A';
        else if (read_rev[j] == 'C') read_rev[j] = 'G';
        else if (read_rev[j] == 'G') read_rev[j] = 'C';
    }
    return read_rev;
}


inline void ksw_align(const char *tseq, int tlen, const char *qseq, int qlen,
               int sc_mch, int sc_mis, int gapo, int gape, ksw_extz_t &ez) {
    int8_t a = sc_mch, b = sc_mis < 0 ? sc_mis : -sc_mis; // a>0 and b<0
    int8_t mat[25] = {a, b, b, b, 0, b, a, b, b, 0, b, b, a, b, 0, b, b, b, a, 0, 0, 0, 0, 0, 0};
    const uint8_t *ts = reinterpret_cast<const uint8_t *>(tseq);
    const uint8_t *qs = reinterpret_cast<const uint8_t *>(qseq);
    memset(&ez, 0, sizeof(ksw_extz_t));
    ksw_extz2_sse(0, qlen, qs, tlen, ts, 5, mat, gapo, gape, -1, -1, 0, 0, &ez);
}

inline int HammingDistance(std::string One, std::string Two)
{
    if (One.length() != Two.length()){
        return -1;
    }

    int counter = 0;

    for(int i=0; i<One.length(); i++) {
        if (One[i] != Two[i]) counter++;
    }

    return counter;
}

static inline void align(std::vector<nam> &nams, std::vector<nam> &nams_rc, std::ofstream &output_file, std::string query_acc, idx_to_acc &acc_map, int k, int read_len, std::vector<unsigned int> &ref_len_map, std::vector<std::string> &ref_seqs, std::string &read, std::string &read_rc ) {
    // Merge fwd and reverse complement hits
    std::vector<nam> all_nams;
    all_nams.reserve( nams.size() + nams_rc.size() ); // preallocate memory
    all_nams.insert( all_nams.end(), nams.begin(), nams.end() );
    all_nams.insert( all_nams.end(), nams_rc.begin(), nams_rc.end() );

    //Sort hits based on start choordinate on query sequence
    std::sort(all_nams.begin(), all_nams.end(), compareByNrHitsAndSimilarSpan);

    // Output results
    int cnt = 0;
    int best_align_dist = INT_MAX;
    int best_align_index = 0; // assume by default it is the nam with most hits and most similar span length
    // Only output single best hit based on: Firstly: number of randstrobe-hits. Secondly the concordance the span of the hits between ref and query (more simmilar ranked higher)
    for (auto &n : all_nams) {
        if (cnt > 5){ // only consider top 5 hits
            break;
        }
        std::string ref_segm = ref_seqs[n.ref_id].substr(n.ref_s - n.query_s, read_len );
//        std::cout << query_acc << ". Ref len:" << ref_segm.length() << " query length: " << read.length() << std::endl;
        if (n.is_rc){
//            o = "-";

            int hamming_dist;
            hamming_dist = HammingDistance(read_rc, ref_segm);
//            std::cout << query_acc  << " Reverse, " << hamming_dist << " " << n.ref_s << " " << n.n_hits << " " << n.query_s << std::endl;
//            std::cout << read_rc << std::endl;
//            std::cout << ref_segm << std::endl;
            if ( (hamming_dist) >=0 && (hamming_dist < 5)) { //Substitutions only
                if (hamming_dist < best_align_dist){
                    best_align_index = cnt;
                    best_align_dist = hamming_dist;
                }

//                std::cout << query_acc << " Reverse: Is exact or subs only: " << hamming_dist << ", ref pos: " << n.ref_s << std::endl;
            }
            else{
                ;
//            ksw_extz_t ez;
//            ksw_align(ptr_ref, len, ptr_read, len, 1, 4, 6, 1, ez);
            }
        }
        else{
//            o = "+";
            int hamming_dist;
            hamming_dist = HammingDistance(read, ref_segm);
//            std::cout << query_acc  << " Forward, " << hamming_dist << " " << n.ref_s << " " << n.n_hits << " " << n.query_s << std::endl;
//            std::cout << read << std::endl;
//            std::cout << ref_segm << std::endl;
            if ( (hamming_dist) >=0 && (hamming_dist < 4)) { //Substitutions only
                if (hamming_dist < best_align_dist){
                    best_align_index = cnt;
                    best_align_dist = hamming_dist;
                }
//                std::cout << query_acc << " Forward: Is exact or subs only: " << hamming_dist << ", ref pos: " << n.ref_s <<  std::endl;
            }
            else{
                ;
//            ksw_extz_t ez;
//            ksw_align(ptr_ref, len, ptr_read, len, 1, 4, 6, 1, ez);
            }
        }



        cnt ++;
    }

    if (all_nams.size() > 0) {
        nam n_best = all_nams[best_align_index];
        std::string o;
        if (n_best.is_rc) {
            o = "-";
        } else {
            o = "+";
        }
        output_file << query_acc << "\t" << read_len << "\t" << n_best.query_s << "\t" << n_best.query_last_hit_pos + k
                    << "\t" << o << "\t" << acc_map[n_best.ref_id] << "\t" << ref_len_map[n_best.ref_id] << "\t"
                    << n_best.ref_s << "\t" << n_best.ref_last_hit_pos + k << "\t" << n_best.n_hits << "\t"
                    << n_best.ref_last_hit_pos + k - n_best.ref_s << "\t" << "-" << "\n";
    }
}


void print_usage() {
    std::cerr << "strobealign [options] <ref.fa> <reads.fasta>\n";
    std::cerr << "options:\n";
    std::cerr << "\t-n INT number of strobes [2]\n";
    std::cerr << "\t-k INT strobe length [20]\n";
    std::cerr << "\t-o name of output SAM-file to print results to [mapped.sam]\n";
//    std::cerr << "\t-x mapping mode\n";
    std::cerr << "\t-w INT minimizer thinning parameter to sample strobes [10]. \n";
    std::cerr << "\t-f FLOAT top fraction of repetitive minimizers to filter out from sampling [0.0002]\n";
}


int main (int argc, char **argv)
{

    if (argc < 3) {
        print_usage();
        return 0;
    }

    // Default parameters
    std::string choice = "randstrobes";
    //    std::string mode = "map";
    std::string mode = "align";

    int n = 2;
    int k = 20;
    int w = 10;
    float f = 0.0002;
    std::string output_file_name;


    int opn = 1;
//    int kmer_temp = 0;
    while (opn < argc) {
        bool flag = false;
        if (argv[opn][0] == '-') {
            if (argv[opn][1] == 'n') {
                n = std::stoi(argv[opn + 1]);
                opn += 2;
                flag = true;
            } else if (argv[opn][1] == 'k') {
                k = std::stoi(argv[opn + 1]);
                opn += 2;
                flag = true;
            } else if (argv[opn][1] == 'o') {
                output_file_name = argv[opn + 1];
                opn += 2;
                flag = true;
            } else if (argv[opn][1] == 'w') {
                w = std::stoi(argv[opn + 1]);
                opn += 2;
                flag = true;
            } else if (argv[opn][1] == 'f') {
                f = std::stof(argv[opn + 1]);
                opn += 2;
                flag = true;
            }
            else {
                print_usage();
            }
        }
        if (!flag)
            break;
    }

    int w_min = k/(w/2);
    int w_max = k/(w/2) + 10;
    int hit_upper_window_lim = (w/2)*w_max;

    std::cout << "Using" << std::endl;
    std::cout << "n: " << n << std::endl;
    std::cout << "k: " << k << std::endl;
    std::cout << "w: " << w << std::endl;
    std::cout << "w_min: " << w_min << std::endl;
    std::cout << "w_max: " << w_max << std::endl;
    std::cout << "[w_min, w_max] under thinning w roughly corresponds to sampling from downstream read coordinates (under random minimizer sampling): [" << (w/2)*w_min << ", " << hit_upper_window_lim << "]" << std::endl;

//    assert(k <= (w/2)*w_min && "k should be smaller than (w/2)*w_min to avoid creating short strobemers");
    assert(k > 7 && "You should really not use too small strobe size!");
    assert(k <= 32 && "k have to be smaller than 32!");
    assert(w > 1 && "w have to be greater than 1!");

    // File name to reference
    std::string filename = argv[opn];
    opn++;

    std::string reads_filename = argv[opn];
//    opn++;



    ///////////////////// INPUT /////////////////////////
//    std::string filename  = "test_ploy2.txt";
//    std::string reads_filename  = "test_ploy2.txt";

//    std::string filename  = "example_repeats.txt";
//    std::string reads_filename  = "example_repeats.txt";

//    std::string filename  = "example3.txt";
//    std::string reads_filename  = "example3.txt";

//    std::string filename  = "ecoli_repeats.txt";
//    std::string reads_filename  = "ecoli_repeats.txt";

//    std::string filename  = "ecoli_bug.txt";
//    std::string reads_filename  = "ecoli_bug.txt";

//    std::string filename  = "ecoli_randmer_bug.txt";
//    std::string reads_filename  = "ecoli_randmer_bug.txt";

//    std::string filename  = "/Users/kxs624/Documents/workspace/strobemers/cmake-build-debug/ecoli.fa";
//    std::string reads_filename  = "ecoli.fa";
//    std::string reads_filename  = "/Users/kxs624/Documents/workspace/strobemers/cmake-build-debug/SRR8187994_1_250k_subset.fasta";
//    std::string reads_filename  = "/Users/kxs624/Documents/workspace/strobemers/cmake-build-debug/SRR8187994_1_50_subset.fasta";
//
//    std::string filename  = "hg38_chr21.fa";
//    std::string reads_filename  = "hg38_chr21.fa";


//    std::string filename  = "/Users/kxs624/Documents/data/genomes/human/chm13_chr21.fa";
//    std::string reads_filename  = "/Users/kxs624/Documents/workspace/StrobeAlign/data/chm13_chr21_reads.fa";

//    std::string filename  = "/Users/kxs624/Documents/data/genomes/human/chm13_chr1.fa";
//    std::string reads_filename  = "/Users/kxs624/Documents/workspace/StrobeAlign/data/chm13_chr1_100k_reads.fa";


//    std::string filename  = "/Users/kxs624/Documents/data/genomes/human/hg38_chr21.fa";
//    std::string filename  = "/Users/kxs624/Documents/workspace/StrobeAlign/data/hg38_chr21_bug_ref.fa";
//    std::string reads_filename  = "/Users/kxs624/Documents/workspace/StrobeAlign/data/hg38_chr21_1M_reads.fa";
//    std::string reads_filename  = "/Users/kxs624/Documents/workspace/StrobeAlign/data/hg38_chr21_reads.fa";
//    std::string reads_filename  = "/Users/kxs624/Documents/workspace/StrobeAlign/data/hg38_chr21_erroneous.fa";
//    std::string reads_filename  = "/Users/kxs624/Documents/workspace/StrobeAlign/data/hg38_chr21_100k_reads.fa";
//    std::string reads_filename  = "/Users/kxs624/Documents/workspace/StrobeAlign/data/hg38_chr21_bug2_reads.fa";


//    std::string filename  = "/Users/kxs624/Documents/data/genomes/human/hg38_chr1.fa";
//    std::string reads_filename  = "/Users/kxs624/Documents/workspace/StrobeAlign/data/hg38_chr1_1M_reads.fa";

//    std::string filename  = "hg21_bug.txt";
//    std::string reads_filename  = "hg21_bug.txt";

//    std::string choice = "kmers";



    std::vector<std::string> ref_seqs;
    std::vector<unsigned int> ref_lengths;
    idx_to_acc acc_map;
    read_references(ref_seqs, ref_lengths, acc_map, filename);


    //////////////////////////////////////////////////////


    //////////// CREATE INDEX OF REF SEQUENCES /////////////////

    // Record index creation start time
    auto start = std::chrono::high_resolution_clock::now();

    one_pos_index tmp_index; // hash table holding all reference mers

    if (choice == "kmers" ){

        for(size_t i = 0; i < ref_seqs.size(); ++i)
        {
            mers_vector kmers; //  kmer hash value, pos, chr_id
            kmers = seq_to_kmers(k, ref_seqs[i], i);
            tmp_index[i] = kmers;
        }
//        for (auto x : ref_seqs){
//            mers_vector kmers; //  kmer hash value, pos, chr_id
//            kmers = seq_to_kmers(k, x.second, x.first);
//            tmp_index[x.first] = kmers;
//        }
    }
    else if (choice == "randstrobes" ){
        if (n == 2 ){
            for(size_t i = 0; i < ref_seqs.size(); ++i)
            {
                mers_vector randstrobes2; // pos, chr_id, kmer hash value
                randstrobes2 = seq_to_randstrobes2(n, k, w_min, w_max, ref_seqs[i], i, w);
                tmp_index[i] = randstrobes2;
            }
//            for (auto x : ref_seqs){
//                mers_vector randstrobes2; // pos, chr_id, kmer hash value
//                randstrobes2 = seq_to_randstrobes2(n, k, w_min, w_max, x.second, x.first, w);
//                tmp_index[x.first] = randstrobes2;
//            }
        }
        else if (n == 3){
            for(size_t i = 0; i < ref_seqs.size(); ++i)
            {
                mers_vector randstrobes3; // pos, chr_id, kmer hash value
                randstrobes3 = seq_to_randstrobes3(n, k, w_min, w_max, ref_seqs[i], i, w);
                tmp_index[i] = randstrobes3;
            }
//            for (auto x : ref_seqs){
//                mers_vector randstrobes3; // pos, chr_id, kmer hash value
//                randstrobes3 = seq_to_randstrobes3(n, k, w_min, w_max, x.second, x.first, w);
//                tmp_index[x.first] = randstrobes3;
//            }
        }
    }
    else {
        std::cout << choice << "not implemented : " << std::endl;
    }


    mers_vector all_mers_vector_tmp;
    all_mers_vector_tmp = construct_flat_vector_three_pos(tmp_index);
    kmer_lookup mers_index_tmp; // k-mer -> (offset in flat_vector, occurence count )
    unsigned int filter_cutoff;
    filter_cutoff = index_vector_one_pos(all_mers_vector_tmp, mers_index_tmp, f); // construct index over flat array
    tmp_index.clear();

    // filter fraction of repetitive strobes
    mers_vector flat_vector_reduced;
    kmer_lookup mers_index;
    filter_repetitive_strobemers(all_mers_vector_tmp, mers_index_tmp, flat_vector_reduced, mers_index, filter_cutoff);
    mers_index_tmp.clear();
    all_mers_vector_tmp.clear();

    filter_cutoff = index_vector_one_pos(flat_vector_reduced, mers_index, f); // construct index over flat array

    mers_vector_reduced all_mers_vector;
    all_mers_vector = remove_kmer_hash_from_flat_vector(flat_vector_reduced);
    flat_vector_reduced.clear();
    print_diagnostics_new4(all_mers_vector, mers_index);
//    std::cout << "Wrote index to disc" << std::endl;

//    std::chrono::milliseconds timespan(10000); // or whatever
//    std::this_thread::sleep_for(timespan);
    //////////////////////////////////////////////////////////////////////////

    // Record index creation end time
    auto finish = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = finish - start;
    std::cout << "Total time generating index: " << elapsed.count() << " s\n" <<  std::endl;

//    std::chrono::milliseconds timespan2(1000000); // or whatever
//    std::this_thread::sleep_for(timespan2);

    ///////////////////////////// MAP ///////////////////////////////////////

    // Record matching time
    auto start_map = std::chrono::high_resolution_clock::now();

    std::ifstream query_file(reads_filename);
    std::ofstream output_file;
    output_file.open (output_file_name);

    std::string line, seq, seq_rc, prev_acc;
    unsigned int q_id = 0;
    unsigned int read_cnt = 0;
    mers_vector query_mers; // pos, chr_id, kmer hash value
    mers_vector query_mers_rc; // pos, chr_id, kmer hash value
    while (getline(query_file, line)) {
        if (line[0] == '>') {
            read_cnt ++;
            if (seq.length() > 0){
                // generate mers here
                if (choice == "kmers" ){
                    query_mers = seq_to_kmers(k, seq, q_id);
                }
                else if (choice == "randstrobes" ){
                    if (n == 2 ){
                        query_mers = seq_to_randstrobes2(n, k, w_min, w_max, seq, q_id, w);
                        seq_rc = reverse_complement(seq);
                        query_mers_rc = seq_to_randstrobes2(n, k, w_min, w_max, seq_rc, q_id, w);
                    }

                    else if (n == 3){
                        query_mers = seq_to_randstrobes3(n, k, w_min, w_max, seq, q_id, w);
                        seq_rc = reverse_complement(seq);
                        query_mers_rc = seq_to_randstrobes3(n, k, w_min, w_max, seq_rc, q_id, w);
                    }
                }
//                std::cout << "HERE " << line << std::endl;
                // Find NAMs
//                std::cout << "Processing read: " << prev_acc << " kmers generated: " << query_mers.size() << ", read length: " <<  seq.length() << std::endl;
                std::vector<nam> nams; // (r_id, r_pos_start, r_pos_end, q_pos_start, q_pos_end)
                std::vector<nam> nams_rc; // (r_id, r_pos_start, r_pos_end, q_pos_start, q_pos_end)
                nams = find_nams(query_mers, all_mers_vector, mers_index, k, ref_seqs, seq, false, hit_upper_window_lim);
                nams_rc = find_nams(query_mers_rc, all_mers_vector, mers_index, k, ref_seqs, seq_rc, true, hit_upper_window_lim);
//                std::cout <<  "NAMs generated: " << nams.size() << std::endl;
                // Output results
                if ( mode.compare("map") == 0) {
//                    output_hits(nams, output_file, prev_acc, acc_map);
//                    output_hits(nams_rc, output_file, prev_acc, acc_map);
                    output_hits_paf(nams, nams_rc, output_file, prev_acc, acc_map, k,  seq.length(), ref_lengths);
//                    output_hits_paf(nams_rc, output_file, prev_acc, acc_map, k, true, seq_rc.length(), ref_lengths);
                }
                else{
                    align(nams, nams_rc, output_file, prev_acc, acc_map, k,  seq.length(), ref_lengths,ref_seqs, seq, seq_rc);
                }
//              output_file << "> " <<  prev_acc << "\n";
//              output_file << "  " << ref_acc << " " << ref_p << " " << q_pos << " " << "\n";
//              outfile.write("  {0} {1} {2} {3}\n".format(ref_acc, ref_p, q_pos, k))
                if (read_cnt % 50000 == 0){
                    std::cout << "Processed " << read_cnt << "reads. " << std::endl;
                }
            }
            prev_acc = line.substr(1, line.length() -1);
            seq = "";
            q_id ++;
        }
        else {
            seq += line;
        }
    }
    if (seq.length() > 0){
        if (choice == "kmers" ){
            query_mers = seq_to_kmers(k, seq, q_id);
        }
        else if (choice == "randstrobes" ){
            if (n == 2 ){
                query_mers = seq_to_randstrobes2(n, k, w_min, w_max, seq, q_id, w);
                seq_rc = reverse_complement(seq);
                query_mers_rc = seq_to_randstrobes2(n, k, w_min, w_max, seq_rc, q_id, w);
            }

            else if (n == 3){
                query_mers = seq_to_randstrobes3(n, k, w_min, w_max, seq, q_id, w);
                seq_rc = reverse_complement(seq);
                query_mers_rc = seq_to_randstrobes3(n, k, w_min, w_max, seq_rc, q_id, w);
            }
        }
        // Find NAMs
//        std::cout << "Processing read: " << prev_acc << " kmers generated: " << query_mers.size() << ", read length: " <<  seq.length() << std::endl;
        std::vector<nam> nams; // (r_id, r_pos_start, r_pos_end, q_pos_start, q_pos_end)
        std::vector<nam> nams_rc; // (r_id, r_pos_start, r_pos_end, q_pos_start, q_pos_end)
        nams = find_nams(query_mers, all_mers_vector, mers_index, k, ref_seqs, seq, false, hit_upper_window_lim);
        nams_rc = find_nams(query_mers_rc, all_mers_vector, mers_index, k, ref_seqs, seq_rc, true, hit_upper_window_lim);
//                std::cout <<  "NAMs generated: " << nams.size() << std::endl;
        // Output results
        if ( mode.compare("map") == 0) {
//                    output_hits(nams, output_file, prev_acc, acc_map);
//                    output_hits(nams_rc, output_file, prev_acc, acc_map);
            output_hits_paf(nams, nams_rc, output_file, prev_acc, acc_map, k, seq.length(), ref_lengths);
//            output_hits_paf(nams_rc, output_file, prev_acc, acc_map, k, true, seq_rc.length(), ref_lengths);
        }
        else{
            align(nams, nams_rc, output_file, prev_acc, acc_map, k,  seq.length(), ref_lengths,ref_seqs, seq, seq_rc);
        }
    }

    query_file.close();
    output_file.close();


    // Record mapping end time
    auto finish_map = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed_map = finish_map - start_map;
    std::cout << "Total time mapping: " << elapsed_map.count() << " s\n" <<  std::endl;


    //////////////////////////////////////////////////////////////////////////


    /////////////////////// FIND AND OUTPUT NAMs ///////////////////////////////





}

