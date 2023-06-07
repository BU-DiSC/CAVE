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
  while (std::getline(file, line)) {
    std::stringstream ss(line);
    std::istream_iterator<std::string> begin(ss);
    std::istream_iterator<std::string> end;
    std::vector<std::string> adj(begin, end);

    std::vector<int> ints;
    std::transform(adj.begin(), adj.end(), std::back_inserter(ints),
                   [&](std::string s) { return stoi(s); });

    GraphNode node;
    node.id = *ints.begin();
    node.key = *ints.begin();

    std::vector<int>::iterator it = ints.begin();
    std::advance(it, 1);

    node.edges.insert(node.edges.end(), it, ints.end());
    node.degree = node.edges.size();
    g->add_node(node);
  }
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
    fprintf(stderr,
            "[ERROR] Usage: ./parser <text file path> (-format:adjlist/edgelist)\n");
    exit(1);
  }

  in_path = std::filesystem::path(argv[1]);
  out_path = std::filesystem::path("..") / "data" / in_path.stem();
  out_path += ".bin";

  for (int i = 2; i < argc; i++) {
    if (strcmp(argv[i], "-format") == 0) {
      if (strcmp(argv[i + 1], "adjlist") == 0) {
        data_format = 1;
      } else if (strcmp(argv[i + 1], "edgelist") == 0) {
        data_format = 2;
      }
    } else if (strcmp(argv[i], "-type") == 0) {
      if (strcmp(argv[i + 1], "undirected") == 0) {
        data_format = 1;
      } else if (strcmp(argv[i + 1], "directed") == 0) {
        data_format = 2;
      }
    }
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
