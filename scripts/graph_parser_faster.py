import networkit as nk
import sys
import os

# If NetworKit package is installed, you could expect 10~100x speedup for parsing.  

if __name__ == '__main__':
    if (len(sys.argv) < 3):
        print('[Usage]: python ./graph_parser_faster.py [format] [file_path]')
        exit(1)

    data_format = sys.argv[1]
    file_path = sys.argv[2]

    if data_format == "edgelist":
        G = nk.readGraph(file_path, nk.graphio.Format.SNAP)
    elif data_format == "adjlist":
        G = nk.readGraph(file_path, nk.graphio.Format.METIS)
    else:
        exit(1)

    print(G.numberOfNodes(), G.numberOfEdges())

    base_name = os.path.basename(file_path).rsplit('.', 1)[0]
    path = os.path.join('..', 'data', '%s.adjlist' % base_name)

    nk.writeGraph(G, path, nk.graphio.Format.METIS)

