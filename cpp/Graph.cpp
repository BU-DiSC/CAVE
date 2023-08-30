#include "Graph.hpp"
#include "Serializer.hpp"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <memory>
#include <queue>
#include <utility>
#include <vector>

#define MIN_NUM_CBLOCKS 16

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

void Graph::_set_cache(int num_cache_blocks) {
  if (num_cache_blocks <= MIN_NUM_CBLOCKS) {
    num_cache_blocks = MIN_NUM_CBLOCKS;
    fprintf(stderr, "[WARNING] Cache size too small, increase to %d.\n",
            MIN_NUM_CBLOCKS);
  }

  if (num_cache_blocks > num_edge_blocks)
    num_cache_blocks = num_edge_blocks;

  fprintf(stderr, "[INFO] Cache size = %d blocks\n", num_cache_blocks);

  edge_cache = new BlockCache<EdgeBlock>(&gs, num_cache_blocks);
  enable_cache = true;
}

void Graph::set_cache(int cache_mb) {
  int num_cache_blocks;
  num_cache_blocks = cache_mb * (1024 * 1024) / sizeof(EdgeBlock);

  _set_cache(num_cache_blocks);
}

void Graph::set_cache(double cache_ratio) {
  int num_cache_blocks = (int)(num_edge_blocks * cache_ratio);

  _set_cache(num_cache_blocks);
}

void Graph::set_cache_mode(CACHE_MODE c_mode) { this->cache_mode = c_mode; }

void Graph::disable_cache() {
  enable_cache = false;
  // printf("[INFO] Cache is disabled.\n");
}

void Graph::clear_cache() {
  if (!enable_cache)
    return;
  edge_cache->clear();
}

void Graph::init_metadata() {
  MetaBlock *meta_block = new MetaBlock();
  gs.read_meta_block(meta_block);

  this->num_nodes = meta_block->num_nodes;
  this->num_vertex_blocks = meta_block->num_vertex_blocks;
  this->num_edge_blocks = meta_block->num_edge_blocks;

  printf("[INFO] |V| = %d\n", num_nodes);
  printf("[INFO] Vertex blocks: %d, Edge blocks: %d\n", num_vertex_blocks,
         num_edge_blocks);

  gs.prep_queue();
}

void Graph::prep_gs() { gs.prep_queue(); }

void Graph::dump_metadata() {
  // Dump metadata
#ifdef _WIN32
  MetaBlock *meta_block =
      (MetaBlock *)_aligned_malloc(sizeof(MetaBlock), BLOCK_SIZE);
#elif __linux__
  MetaBlock *meta_block =
      (MetaBlock *)aligned_alloc(BLOCK_SIZE, sizeof(MetaBlock));
#endif
  memset(meta_block, 0, sizeof(MetaBlock));

  meta_block->num_nodes = num_nodes;
  meta_block->num_blocks = num_vertex_blocks + num_edge_blocks;
  meta_block->num_vertex_blocks = num_vertex_blocks;
  meta_block->num_edge_blocks = num_edge_blocks;

  printf("[INFO] # Nodes: %d, Vertex block: %d, Edge blocks: %d\n", num_nodes,
         num_vertex_blocks, num_edge_blocks);

  gs.write_meta_block(meta_block);
}

void Graph::dump_vertices() {

  uint32_t VB_CAPA = sizeof(VertexBlock) / (2 * sizeof(int));
  uint32_t EB_CAPA = sizeof(EdgeBlock) / sizeof(int);
  printf("[INFO] VB capacity = %u, EB = %u\n", VB_CAPA, EB_CAPA);

  num_vertex_blocks = (num_nodes - 1) / VB_CAPA + 1;
  std::vector<VertexBlock> vertex_blocks(num_vertex_blocks);

  int vb_id = 0;
  int vb_offset = 0;

  std::vector<EdgeBlock> edge_blocks;
  SegmentTree eb_tree(2 * (num_edges / EB_CAPA), EB_CAPA);

  for (int i = 0; i < num_nodes; i++) {
    // Vertex block is full
    if (vb_offset == VB_CAPA) {
      vb_offset = 0;
      vb_id++;
    }

    Vertex &v = vertex_blocks[vb_id].vertices[vb_offset];
    v.degree = nodes[i].degree;
    // v.key = nodes[i].key;
    vb_offset++;

    if (v.degree > EB_CAPA) {
      // Create new block(s)
      v.edge_block_idx_off = edge_blocks.size() << EB_DIGITS;
      // v.edge_block_id = edge_blocks.size();
      // v.edge_block_offset = 0;

      int deg_offset = 0;

      while (deg_offset < v.degree) {
        int tmp_degree = std::min(v.degree - deg_offset, EB_CAPA);
        edge_blocks.emplace_back();
        EdgeBlock &eb = edge_blocks.back();
        memcpy(eb.edges, nodes[i].edges.data() + deg_offset,
               tmp_degree * sizeof(int));
        deg_offset += tmp_degree;

        int bid = edge_blocks.size() - 1;
        int new_capa = EB_CAPA - tmp_degree;
        if (new_capa > 0) {
          // Find an empty block
          int tnode_id = eb_tree.query_first_larger(EB_CAPA);
          if (tnode_id == -1) {
            exit(1);
          }
          eb_tree.update_id(tnode_id, new_capa, bid);
        }
      }
    } else {
      int tnode_id = eb_tree.query_first_larger(v.degree);
      if (tnode_id == -1) {
        exit(1);
      }
      int offset = EB_CAPA - eb_tree.get_val(tnode_id);
      int bid = eb_tree.get_val2(tnode_id);

      if (bid == -1) {
        edge_blocks.emplace_back();
        bid = edge_blocks.size() - 1;
      }

      EdgeBlock &eb = edge_blocks[bid];
      memcpy(eb.edges + offset, nodes[i].edges.data(), v.degree * sizeof(int));

      v.edge_block_idx_off = offset + (bid << EB_DIGITS);
      // v.edge_block_id = bid;
      // v.edge_block_offset = offset;

      int new_capa = EB_CAPA - offset - v.degree;
      eb_tree.update_id(tnode_id, new_capa, bid);
    }
  }

  printf("[INFO] Edge packing finished, size %zu\n", edge_blocks.size());

  int batch_size = 1024;

  // Write vertex blocks
  for (int i = 0; i < vertex_blocks.size(); i += batch_size) {
    size_t k = i + batch_size;
    if (k > vertex_blocks.size())
      k = vertex_blocks.size();
    gs.write_blocks<VertexBlock>(i, &vertex_blocks[i], k - i);
  }

  // Write edge blocks
  for (int i = 0; i < edge_blocks.size(); i += batch_size) {
    size_t k = i + batch_size;
    if (k > edge_blocks.size())
      k = edge_blocks.size();
    gs.write_blocks<EdgeBlock>(num_vertex_blocks + i, &edge_blocks[i], k - i);
  }

  num_edge_blocks = edge_blocks.size();
  printf("[INFO] Writing edge blocks finished.\n");
}

int Graph::get_num_nodes() { return num_nodes; }

void Graph::dump_graph() {
  // Sum up total number of nodes and edges
  num_nodes = nodes.size();
  num_edges = 0;
  for (int i = 0; i < num_nodes; i++) {
    num_edges += nodes[i].degree;
  }

  printf("[INFO] |V| = %d, |E| = %llu\n", num_nodes, num_edges);

  prep_gs();

  this->dump_vertices();
  this->dump_metadata();
  gs.finish_write();
}

void Graph::init_vertex_data() { this->read_vertex_blocks(); }

void Graph::set_node_edges(int node_id, std::vector<int> &edges) {
  nodes[node_id].edges = edges;
  nodes[node_id].degree = edges.size();
}

void Graph::add_edge(int src_id, int dst_id) {
  if (reorder_node_id.find(src_id) == reorder_node_id.end()) {
    reorder_node_id[src_id] = tmp_node_id;
    if (tmp_node_id >= nodes.size()) {
      nodes.emplace_back();
      GraphNode &tmp_node = nodes.back();
      tmp_node.id = tmp_node.key = tmp_node_id;
    }
    tmp_node_id++;
  }
  if (reorder_node_id.find(dst_id) == reorder_node_id.end()) {
    reorder_node_id[dst_id] = tmp_node_id;
    if (tmp_node_id >= nodes.size()) {
      nodes.emplace_back();
      GraphNode &tmp_node = nodes.back();
      tmp_node.id = tmp_node.key = tmp_node_id;
    }
    tmp_node_id++;
  }
  if (tmp_node_id > nodes.size()) {
    fprintf(stderr, "[ERROR] Too many nodes: %d > %ld\n", tmp_node_id,
            nodes.size());
    exit(1);
  }
  nodes[reorder_node_id[src_id]].edges.push_back(reorder_node_id[dst_id]);
  // nodes[src_id].edges.push_back(dst_id);
}
void Graph::init_nodes(int _num_nodes) {
  this->num_nodes = _num_nodes;
  nodes = std::vector<GraphNode>(num_nodes);
  for (int i = 0; i < num_nodes; i++) {
    nodes[i].id = i;
    nodes[i].key = i;
  }
}
void Graph::finalize_edgelist() {
  num_nodes = nodes.size();
  fprintf(stderr, "[INFO] Final |V| = %d\n", num_nodes);
  for (int i = 0; i < num_nodes; i++) {
    nodes[i].degree = nodes[i].edges.size();
  }
}

void Graph::clear_nodes() {
  nodes.clear();
  num_nodes = 0;
}

void Graph::read_vertex_blocks() {
  vb_vec = std::vector<VertexBlock>(num_vertex_blocks);
  gs.read_blocks(0, num_vertex_blocks, &vb_vec);
}

int Graph::get_node_key(int node_id) { return node_id; }

uint32_t Graph::get_node_degree(int node_id) {
  if (node_id < 0 || node_id > num_nodes) {
    printf("[ERROR] Bad Node Id = %d\n", node_id);
    exit(1);
  }

  Vertex v;
  int block_id = node_id / VB_CAPACITY;
  int block_offset = node_id % VB_CAPACITY;
  v = vb_vec[block_id].vertices[block_offset];

  return v.degree;
}

std::vector<uint32_t> Graph::get_edges(int node_id) {
  // printf("node_id = %d\n", node_id);
  if (node_id < 0 || node_id > num_nodes) {
    printf("[ERROR] Bad Node Id = %d\n", node_id);
    exit(1);
  }
  int block_id = node_id / VB_CAPACITY;
  int block_offset = node_id % VB_CAPACITY;

  Vertex v = vb_vec[block_id].vertices[block_offset];
  // int eb_id = v.edge_block_id;
  // int eb_offset = v.edge_block_offset;
  unsigned int eb_id = v.edge_block_idx_off >> EB_DIGITS;
  unsigned int eb_offset = v.edge_block_idx_off & ((1 << EB_DIGITS) - 1);

  std::vector<uint32_t> edges(v.degree);

  int first_block_id = eb_id + num_vertex_blocks;

  // Single block
  if (v.degree <= EB_CAPACITY - eb_offset) {
    // printf("Single block: ");
    EdgeBlock *eb_ptr;
    if (!enable_cache) {
      // Directly read from disks
      eb_ptr = new EdgeBlock();
      gs.read_block<EdgeBlock>(first_block_id, eb_ptr);
      std::memcpy(edges.data(), eb_ptr->edges + eb_offset,
                  v.degree * sizeof(int));
    } else {
      // Read from cache
      int cb_idx = edge_cache->request_block(first_block_id);
      eb_ptr = edge_cache->get_cache_block(cb_idx, first_block_id);
      std::memcpy(edges.data(), eb_ptr->edges + eb_offset,
                  v.degree * sizeof(int));
      edge_cache->release_cache_block(cb_idx, eb_ptr);
    }
  } else {
    // Multiple blocks
    int count_blocks = (v.degree + eb_offset - 1) / EB_CAPACITY + 1;
    if (!enable_cache) {
      // Directly read from disks
      std::vector<EdgeBlock> eb_vec(count_blocks);
      gs.read_blocks<EdgeBlock>(first_block_id, count_blocks, &eb_vec);
      std::memcpy(edges.data(), eb_vec.data(), v.degree * sizeof(int));
    } else {
      // Read from cache
      int edges_in_vec = (count_blocks - 1) * EB_CAPACITY;
      int edges_left = v.degree - edges_in_vec;
      std::vector<EdgeBlock> eb_vec(count_blocks - 1);

      gs.read_blocks<EdgeBlock>(first_block_id, count_blocks - 1, &eb_vec);
      std::memcpy(edges.data(), eb_vec.data(), edges_in_vec * sizeof(int));

      int cb_idx = edge_cache->request_block(first_block_id + count_blocks - 1);
      EdgeBlock *last_block = edge_cache->get_cache_block(
          cb_idx, first_block_id + count_blocks - 1);
      std::memcpy(edges.data() + edges_in_vec, last_block,
                  edges_left * sizeof(int));
      edge_cache->release_cache_block(cb_idx, last_block);
    }
  }
  return edges;
};

uint64_t Graph::get_data_mb() { return gs.get_size_mb(); }
