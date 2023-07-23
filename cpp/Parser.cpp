#include "Graph.hpp"
#include "Serializer.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

std::filesystem::path in_path, out_path;
int data_format = -1;

void read_adjlist(std::ifstream &file, Graph *g) {
  std::string line;
  int count = 0;
  int expected_num_nodes, expected_num_edges;

  while (std::getline(file, line)) {
    std::stringstream ss(line);
    std::istream_iterator<std::string> begin(ss);
    std::istream_iterator<std::string> end;
    std::vector<std::string> adj(begin, end);

    std::vector<int> ints;
    std::transform(adj.begin(), adj.end(), std::back_inserter(ints),
                   [&](std::string s) { return stoi(s); });

    if (count == 0) { // First line of adjlist file should be |V| |E|
      expected_num_nodes = ints.at(0);
      expected_num_edges = ints.at(1);
      fprintf(stderr, "[INFO] Expected |V| = %d, |E| = %d\n",
              expected_num_nodes, expected_num_edges);
      g->init_nodes(expected_num_nodes);
    } else {
      // A workaround for METIS format, which has a start node id of 1.
      std::for_each(ints.begin(), ints.end(), [](int &k) { k--; });
      g->set_node_edges(count - 1, ints);
    }
    count++;
  }

  assert(expected_num_nodes + 1 == count);
}

void read_edgelist(std::ifstream &file, Graph *g) {
  std::string line;

  while (std::getline(file, line)) {
    std::stringstream ss(line);
    std::istream_iterator<std::string> begin(ss);
    std::istream_iterator<std::string> end;
    std::vector<std::string> str_list(begin, end);

    if (std::strcmp(str_list[0].data(), "#") == 0) {
      if (str_list.size() > 1) {
        // Top lines
        if (std::strcmp(str_list[1].data(), "Nodes:") == 0) {
          int expected_num_nodes = std::stoi(str_list[2]);
          int expected_num_edges = std::stoi(str_list[4]);
          g->init_nodes(expected_num_nodes);
          fprintf(stderr, "[INFO] Expected |V| = %d, |E| = %d\n",
                  expected_num_nodes, expected_num_edges);
        }
        // else if (std::strcmp(str_list[1].data(), "Directed") == 0) {
        //   graph_type = 0;
        // } else if (std::strcmp(str_list[1].data(), "Undirected") == 0) {
        //   graph_type = 1;
        // }
      }
    } else if (std::strcmp(str_list[0].data(), "p") == 0) {
      int expected_num_nodes = std::stoi(str_list[1]);
      int expected_num_edges = std::stoi(str_list[2]);
      fprintf(stderr, "[INFO] Expected |V| = %d, |E| = %d\n",
              expected_num_nodes, expected_num_edges);
      g->init_nodes(expected_num_nodes);
    } else {
      std::vector<int> ints;
      std::transform(str_list.begin(), str_list.end(), std::back_inserter(ints),
                     [&](std::string s) { return stoi(s); });
      g->add_edge(ints[0], ints[1]);
    }
  }
  g->finalize_edgelist();
}

void read_binary_edgelist(std::ifstream &file, Graph *g) {
  std::vector<int> two_ids(2);

  while (file.read(reinterpret_cast<char *>(two_ids.data()), 2 * sizeof(int))) {
    g->add_edge(two_ids[0], two_ids[1]);
  }
  g->finalize_edgelist();
}

void read_binary_adjlist(std::ifstream &file, Graph *g) {
  int expected_num_nodes, expected_num_edges;

  file.read(reinterpret_cast<char *>(&expected_num_nodes), sizeof(int));
  file.read(reinterpret_cast<char *>(&expected_num_edges), sizeof(int));

  fprintf(stderr, "[INFO] Expected |V| = %d, |E| = %d\n", expected_num_nodes,
          expected_num_edges);

  g->init_nodes(expected_num_nodes);

  int tmp_degree;
  int src_id = 0;

  while (file.read(reinterpret_cast<char *>(&tmp_degree), sizeof(int))) {

    std::vector<int> ints(tmp_degree);
    file.read(reinterpret_cast<char *>(ints.data()), tmp_degree * sizeof(int));

    std::for_each(ints.begin(), ints.end(), [](int &k) { k--; });
    g->set_node_edges(src_id, ints);

    src_id++;
  }

  assert(expected_num_nodes == src_id);
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "[ERROR] Usage: ./parser <text file path> "
                    "(-format:adjlist/edgelist)\n");
    exit(1);
  }

  in_path = std::filesystem::path(argv[1]);
  std::string data_extension = in_path.extension().string().substr(1);

  out_path = std::filesystem::path("..") / "data" / in_path.stem();
  out_path += ".bin";

  for (int i = 2; i < argc; i++) {
    std::string temp_arg = argv[i];
    if (temp_arg == "-format" && i + 1 < argc) {
      data_extension = argv[i + 1];
    }
  }

  if (data_extension == "adjlist") {
    data_format = 1;
  } else if (data_extension == "edgelist") {
    data_format = 2;
  } else if (data_extension == "binedge") {
    data_format = 3;
  } else if (data_extension == "binadj") {
    data_format = 4;
  } else {
    fprintf(stderr, "[ERROR]: \'%s\' is not a supported data extension.\n",
            data_extension.data());
    exit(1);
  }

  std::ifstream file;

  printf("%s -> %s\n", in_path.string().data(), out_path.string().data());
  Graph *g = new Graph();

  auto begin = std::chrono::high_resolution_clock::now();

  switch (data_format) {
  case 1:
    file.open(in_path.string().data());
    read_adjlist(file, g);
    break;
  case 2:
    file.open(in_path.string().data());
    read_edgelist(file, g);
    break;
  case 3:
    file.open(in_path.string().data(), std::ios_base::binary);
    read_binary_edgelist(file, g);
    break;
  case 4:
    file.open(in_path.string().data(), std::ios_base::binary);
    read_binary_adjlist(file, g);
    break;
  default:
    printf("[ERROR] Please indicate right data format.\n");
    exit(1);
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto ms_int =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - begin)
          .count();

  printf("[INFO] Read input files takes %lld ms.\n", ms_int);

  begin = std::chrono::high_resolution_clock::now();
  // Dump graph
  g->init_serializer(out_path.string().data(), MODE::WRITE);
  g->dump_graph();

  end = std::chrono::high_resolution_clock::now();
  ms_int = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin)
               .count();
  printf("[INFO] Dump graph takes %lld ms.\n", ms_int);

  return 0;
}
