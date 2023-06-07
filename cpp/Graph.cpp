#include "Graph.hpp"
#include "Serializer.hpp"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <memory>
#include <queue>
#include <utility>
#include <vector>

void Graph::init_serializer(std::string path, MODE mode) {
  if (mode == MODE::INVALID) {
    gs_init = false;
    return;
  }
  if (mode == MODE::IN_MEMORY)
    enable_cache = false;
  gs.open_file(path, mode);
  gs_init = true;
}

void Graph::clear_serializer() {
  if (gs_init)
    gs.clear();
  gs_init = false;
}

void Graph::set_cache(int cache_size_mb) {
  enable_cache = true;
  int cache_size = cache_size_mb * 256; // 1 MB / 4 KB
  edge_cache = new BlockCache<EdgeBlock>(&gs, cache_size);
  printf("[INFO]: Cache size = %d blocks\n", cache_size);
}

void Graph::set_cache(double cache_ratio) {
  enable_cache = true;
  int cache_size = (int)(num_edge_blocks * cache_ratio);
  if (cache_size < 1)
    cache_size = 1;
  edge_cache = new BlockCache<EdgeBlock>(&gs, cache_size);
  // printf("[INFO]: Cache size = %d blocks\n", cache_size);
}

void Graph::set_cache_mode(CACHE_MODE c_mode) { this->cache_mode = c_mode; }

void Graph::disable_cache() {
  enable_cache = false;
  // printf("[INFO]: Cache is disabled.\n");
}

void Graph::clear_cache() {
  if (!enable_cache)
    return;
  edge_cache->clear();
}

void Graph::init_metadata() {
  auto meta_block = gs.read_block<MetaBlock>(0);
  num_nodes = meta_block->num_nodes;
  num_vertex_blocks = meta_block->num_vertex_blocks;
  num_edge_blocks = meta_block->num_edge_blocks;
  printf("[INFO]: Vertex blocks: %d, Edge blocks: %d\n", num_vertex_blocks,
         num_edge_blocks);

  gs.prep_queue();
}

// void Graph::clear_signals() { gs.clear_signals(); }

void Graph::prep_gs() { gs.prep_queue(); }

void Graph::dump_metadata() {
  // Dump metadata
#ifdef _WIN32
  MetaBlock *meta_block =
      (MetaBlock *)_aligned_malloc(sizeof(MetaBlock), S_BLOCK_SIZE);
#elif __linux__
  MetaBlock *meta_block =
      (MetaBlock *)aligned_alloc(S_BLOCK_SIZE, sizeof(MetaBlock));
#endif

  meta_block->num_nodes = num_nodes;
  meta_block->num_blocks =
      num_vertex_blocks + num_edge_blocks + 1; // add metadata block
  meta_block->num_vertex_blocks = num_vertex_blocks;
  meta_block->num_edge_blocks = num_edge_blocks;

  printf("[INFO] # Nodes: %d, Vertex block: %d, Edge blocks: %d\n", num_nodes,
         num_vertex_blocks, num_edge_blocks);

  gs.write_block(0, meta_block, sizeof(MetaBlock));
}

EdgeBlock *new_edge_block() {
#ifdef _WIN32
  EdgeBlock *eb = (EdgeBlock *)_aligned_malloc(sizeof(EdgeBlock), S_BLOCK_SIZE);
#elif __linux__
  EdgeBlock *eb = (EdgeBlock *)aligned_alloc(S_BLOCK_SIZE, sizeof(EdgeBlock));
#endif
  return eb;
}

VertexBlock *new_vertex_block() {
#ifdef _WIN32
  VertexBlock *vb =
      (VertexBlock *)_aligned_malloc(sizeof(VertexBlock), S_BLOCK_SIZE);
#elif __linux__
  VertexBlock *vb =
      (VertexBlock *)aligned_alloc(S_BLOCK_SIZE, sizeof(VertexBlock));
#endif
  return vb;
}

void Graph::dump_vertices() {
  std::vector<VertexBlock *> vertex_blocks;

  vertex_blocks.push_back(new_vertex_block());
  num_vertex_blocks = (num_nodes - 1) / VB_CAPACITY + 1;
  int vb_id = 0;
  int vb_offset = 0;

  std::sort(nodes.begin(), nodes.end(), [](GraphNode &a, GraphNode &b) {
    if (a.degree != b.degree)
      return a.degree > b.degree;
    return a.id < b.id;
  });

  int total_deg = 0;
  for (int i = 0; i < num_nodes; i++) {
    total_deg += nodes[i].degree;
  }

  printf("[INFO] Total degrees: %d\n", total_deg);

  std::vector<EdgeBlock *> edge_blocks;
  SegmentTree eb_tree(2 * (total_deg / EB_CAPACITY), EB_CAPACITY);

  for (int i = 0; i < num_nodes; i++) {
    // Vertex block is full
    if (vb_offset == VB_CAPACITY) {
      vb_offset = 0;
      vb_id++;
      vertex_blocks.push_back(new_vertex_block());
    }

    Vertex &v = vertex_blocks[vb_id]->vertices[vb_offset];
    v.degree = nodes[i].degree;
    v.key = nodes[i].key;
    vb_offset++;

    if (v.degree > EB_CAPACITY) {
      // Create new block(s)
      v.edge_block_id = edge_blocks.size();
      v.edge_block_offset = 0;

      int deg_offset = 0;

      while (deg_offset < v.degree) {
        int tmp_degree = std::min(v.degree - deg_offset, EB_CAPACITY);
        auto eb = new_edge_block();
        memcpy(eb->edges, nodes[i].edges.data() + deg_offset,
               tmp_degree * sizeof(int));
        deg_offset += tmp_degree;
        edge_blocks.push_back(eb);

        int bid = edge_blocks.size() - 1;
        int new_capa = EB_CAPACITY - tmp_degree;
        if (new_capa > 0) {
          // Find an empty block
          int tnode_id = eb_tree.query_first_larger(EB_CAPACITY);
          if (tnode_id == -1) {
            exit(1);
          }
          // printf("tnode_id = %d\n", tnode_id);
          eb_tree.update_id(tnode_id, new_capa, bid);
        }
      }
    } else {
      int tnode_id = eb_tree.query_first_larger(v.degree);
      // printf("tnode_id = %d\n", tnode_id);
      if (tnode_id == -1) {
        exit(1);
      }
      int offset = EB_CAPACITY - eb_tree.get_val(tnode_id);
      int bid = eb_tree.get_val2(tnode_id);

      if (bid == -1) {
        auto eb = new_edge_block();
        edge_blocks.push_back(eb);
        bid = edge_blocks.size() - 1;
      }

      auto eb = edge_blocks[bid];
      memcpy(eb->edges + offset, nodes[i].edges.data(), v.degree * sizeof(int));
      v.edge_block_id = bid;
      v.edge_block_offset = offset;

      int new_capa = EB_CAPACITY - offset - v.degree;
      eb_tree.update_id(tnode_id, new_capa, bid);
    }
  }

  printf("[INFO] Edge packing finished, size %zu\n", edge_blocks.size());

  assert(vertex_blocks.size() == num_vertex_blocks);

  for (int i = 0; i < vertex_blocks.size(); i++) {
    gs.write_block(1 + i, vertex_blocks[i], sizeof(VertexBlock));
  }

  for (int i = 0; i < edge_blocks.size(); i++) {
    gs.write_block(num_vertex_blocks + 1 + i, edge_blocks[i],
                   sizeof(EdgeBlock));
  }

  num_edge_blocks = edge_blocks.size();
}

int Graph::get_num_nodes() { return num_nodes; }

void Graph::dump_graph() {
  num_nodes = nodes.size();
  prep_gs();
  this->dump_vertices();
  this->dump_metadata();
  gs.finish_write();
}

bool Graph::wait_all_signals() { return gs.wait_all_signals(); }

void Graph::init_vertex_data() { this->read_vb_list(); }

void Graph::add_node(GraphNode& g_node) { nodes.push_back(g_node); }
void Graph::add_edge(int node_id1, int node_id2) {
  if (reorder_node_id.count(node_id1) == 0) {
    reorder_node_id[node_id1] = tmp_node_id++;
  }
  if (reorder_node_id.count(node_id2) == 0) {
    reorder_node_id[node_id2] = tmp_node_id++;
  }
  if (tmp_node_id > nodes.size()) {
    printf("[ERROR] Too many nodes.\n");
    exit(1);
  }
  nodes[reorder_node_id[node_id1]].edges_set.insert(reorder_node_id[node_id2]);
}
void Graph::init_nodes(int _num_nodes) {
  this->num_nodes = _num_nodes;
  nodes = std::vector<GraphNode>(num_nodes);
  for (int i = 0; i < num_nodes; i++) {
    nodes[i].id = i;
    nodes[i].key = i;
  }
  tmp_node_id = 0;
}
void Graph::finalize_edgelist() {
  for (int i = 0; i < num_nodes; i++) {
    nodes[i].edges =
        std::vector<int>(nodes[i].edges_set.begin(), nodes[i].edges_set.end());
    nodes[i].degree = nodes[i].edges.size();
  }
}

void Graph::clear_nodes() {
  nodes.clear();
  num_nodes = 0;
}

void Graph::read_vb_list() {
  vb_list = gs.read_blocks<VertexBlock>(1, num_vertex_blocks);
}

int Graph::get_node_key(int node_id) {
  int block_id = node_id / VB_CAPACITY;
  int block_offset = node_id % VB_CAPACITY;

  auto v = vb_list[block_id]->vertices[block_offset];
  return v.key;
}

int Graph::get_node_degree(int node_id) {
  int block_id = node_id / VB_CAPACITY;
  int block_offset = node_id % VB_CAPACITY;

  auto v = vb_list[block_id]->vertices[block_offset];
  return v.degree;
}

std::vector<int> Graph::get_edges(int node_id) {
  int block_id = node_id / VB_CAPACITY;
  int block_offset = node_id % VB_CAPACITY;

  Vertex &v = vb_list[block_id]->vertices[block_offset];
  int eb_id = v.edge_block_id;
  int eb_offset = v.edge_block_offset;

  std::vector<int> edges(v.degree);

  int first_block_id = eb_id + 1 + num_vertex_blocks;

  // Single block
  if (v.degree <= EB_CAPACITY - eb_offset) {
    std::shared_ptr<EdgeBlock> edge_block;
    if (!enable_cache) {
      // Directly read from disks
      edge_block = gs.read_block<EdgeBlock>(first_block_id);
    } else {
      // Read from cache
      edge_block = edge_cache->get_block(first_block_id);
    }
    std::memcpy(edges.data(), edge_block->edges + eb_offset,
                v.degree * sizeof(int));
  } else {
    // Multiple blocks
    std::vector<std::shared_ptr<EdgeBlock>> edge_blocks;
    int count_blocks = 1 + (v.degree + eb_offset - 1) / EB_CAPACITY;
    // printf("[INFO] %d Read %d -> %d blocks\n", v.degree, first_block_id,
    //        first_block_id + count_blocks - 1);
    if (!enable_cache) {
      // Directly read from disks
      edge_blocks = gs.read_blocks<EdgeBlock>(first_block_id, count_blocks);
    } else {
      // Read from cache
      if (cache_mode == CACHE_MODE::ALL_CACHE) {
        edge_blocks = edge_cache->get_blocks(first_block_id, count_blocks);
      } else {
        edge_blocks =
            gs.read_blocks<EdgeBlock>(first_block_id, count_blocks - 1);
        auto last_block =
            edge_cache->get_block(first_block_id + count_blocks - 1);
        edge_blocks.push_back(last_block);
      }
    }
    int degree_left = v.degree;
    int dest_pos = 0;
    for (int i = 0; i < count_blocks; i++) {
      int tmp_degree = std::min(degree_left, EB_CAPACITY - eb_offset);
      std::memcpy(edges.data() + dest_pos, edge_blocks[i]->edges + eb_offset,
                  tmp_degree * sizeof(int));
      degree_left -= tmp_degree;
      dest_pos += tmp_degree;
      eb_offset = 0;
    }
  }
  return edges;
};
