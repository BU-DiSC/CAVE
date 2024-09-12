import networkit as nk
import sys
import os
import struct

def write_binary_adj(G, data_name):
    # Binary adjacent list
    path = os.path.join('..', 'data', "%s.binadj" % data_name)
    with open(path, "wb") as out_file:
        out_file.write(struct.pack("<l", G.numberOfNodes()))
        out_file.write(struct.pack("<l", G.numberOfEdges()))
        for u in G.iterNodes():
            out_file.write(struct.pack("<l", G.degree(u)))
            for v in G.iterNeighbors(u):
                out_file.write(struct.pack("<l", v))

def write_binary_edge(G, data_name):
    # Binary edge list
    path = os.path.join('..', 'data', "%s.binedge" % data_name)
    with open(path, "wb") as out_file:
        for (u, v) in G.iterEdges():
            out_file.write(struct.pack("<l", u))
            out_file.write(struct.pack("<l", v))

            # Self loop
            if u != v:
                out_file.write(struct.pack("<l", v))
                out_file.write(struct.pack("<l", u))

def write_texts(G, data_name):
    # Write adjacent list
    path = os.path.join('..', 'data', "%s.adjlist" % data_name)
    nk.graphio.writeGraph(G, path, nk.graphio.Format.METIS)

    # Write edge list
    path = os.path.join('..', 'data', "%s.edgelist" % data_name)
    nk.graphio.writeGraph(G, path, nk.graphio.Format.EdgeList, ' ', 0, '#')


if __name__ == '__main__':
    if (len(sys.argv) < 3):
        print('[Usage]: python ./graph_parser_faster.py [format] [file_path]')
        exit(1)

    data_format = sys.argv[1]
    file_path = sys.argv[2]

    if data_format == "SNAP":
        G = nk.readGraph(file_path, nk.graphio.Format.EdgeList,
                         separator='\t', commentPrefix='#', continuous=False)
    elif data_format == "adjlist":
        G = nk.readGraph(file_path, nk.graphio.Format.METIS)
    else:
        exit(1)

    # G = nk.graphtools.toUndirected(G)
    # G.removeMultiEdges()
    # G.removeSelfLoops()
    print(G.numberOfNodes(), G.numberOfEdges())

    base_name = os.path.basename(file_path).rsplit('.', 1)[0]

    write_binary_edge(G, base_name)
    # write_binary_adj(G, base_name)
    # write_texts(G, base_name)
