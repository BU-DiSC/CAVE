#include "Graph.hpp"
#include "Serializer.hpp"
#include <algorithm>
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
int graph_type = 0;

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
          int num_nodes = std::stoi(str_list[2]);
          g->init_nodes(num_nodes);
        } else if (std::strcmp(str_list[1].data(), "Directed") == 0) {
          graph_type = 0;
        } else if (std::strcmp(str_list[1].data(), "Undirected") == 0) {
          graph_type = 1;
        }
      }
    } else {
      std::vector<int> ints;
      std::transform(str_list.begin(), str_list.end(), std::back_inserter(ints),
                     [&](std::string s) { return stoi(s); });
      g->add_edge(ints[0], ints[1]);
      g->add_edge(ints[1], ints[0]);
      // if (graph_type == 1) {
      //   g->add_edge(ints[1], ints[0]);
      // }
    }
  }
  g->finalize_edgelist();
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
  } else {
    fprintf(stderr, "[ERROR]: \'%s\' is not a supported data extension.\n",
            data_extension.data());
    exit(1);
  }

  std::ifstream file;
  file.open(in_path.string().data());

  printf("%s -> %s\n", in_path.string().data(), out_path.string().data());
  Graph *g = new Graph();

  switch (data_format) {
  case 1:
    read_adjlist(file, g);
    break;
  case 2:
    read_edgelist(file, g);
    break;
  default:
    printf("[ERROR] Please indicate right data format.\n");
    exit(1);
  }

  // Dump graph
  g->init_serializer(out_path.string().data(), MODE::WRITE);
  g->dump_graph();

  return 0;
}
