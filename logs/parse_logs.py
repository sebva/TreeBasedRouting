#!/usr/bin/env python3
import os
import xml
from xml.sax.handler import ContentHandler

import re

all_nodes = {}


class Node:
    def __init__(self, nid, x=0, y=0, z=0):
        self.nid = nid
        self.x = x
        self.y = y
        self.z = z
        self.parent = None
        self.children = []

    def __repr__(self, *args, **kwargs):
        return "%d, %d, %d, %d" % (self.nid, self.x, self.y, self.z)

    def add_child(self, child):
        self.children.append(child)

    def remove_child(self, child):
        self.children.remove(child)

    @staticmethod
    def parse_urn_id(urn):
        m = re.search("urn:wisebed:node:ubern:([0-9]+)", urn)
        return int(m.group(1))


class XmlHandler(ContentHandler):
    def __init__(self):
        super().__init__()
        self.location = []

    def startElement(self, name, attrs):
        self.location.append(name)

    def endElement(self, name):
        self.location.pop()


class InstantiateNodesHandler(XmlHandler):
    def startElement(self, name, attrs):
        super().startElement(name, attrs)

        if self.location == ['wiseml', 'setup', 'node']:
            nid = Node.parse_urn_id(attrs['id'])
            self.node = Node(nid)
            all_nodes[nid] = self.node

    def characters(self, content):
        if self.location[:4] == ['wiseml', 'setup', 'node', 'position'] and len(self.location) == 5:
            setattr(self.node, self.location[4], int(content))


class BuildTreeHandler(XmlHandler):
    def startElement(self, name, attrs):
        super().startElement(name, attrs)

        if self.location == ['wiseml', 'trace', 'node']:
            nid = Node.parse_urn_id(attrs['id'])
            self.node = all_nodes[nid]

    def characters(self, content):
        if self.location == ['wiseml', 'trace', 'node', 'data']:
            m = re.search("New parent node: ([0-9]+)", content)
            if m is not None:
                if self.node.parent is not None:
                    self.node.parent.remove_child(self.node)

                parent = all_nodes[int(m.group(1))]
                self.node.parent = parent
                parent.add_child(self.node)


def print_tree(node, level=0):
    print("|\t" * level, end='')
    print(node.nid)
    for child in node.children:
        print_tree(child, level + 1)


def tikz_relations(node, file_handle):
    for child in node.children:
        file_handle.write('\draw [red, thick, <-, >=latex] (%d,%d,%d) to (%d,%d,%d);' % (node.x, node.y, node.z, child.x, child.y, child.z))
        tikz_relations(child, file_handle)


def export_tikz(file_handle):
    file_handle.write(r"""
    \begin{tikzpicture}[scale=0.097,y= {(0.281cm,0.281cm)}, z={(0cm,1cm)}, x={(1cm,0cm)}]]
    \node[anchor=south west,inner sep=0] at (0,0) {\includegraphics[width=\textwidth]{3D_image_iam_complete.png}};
    """)
    tikz_relations(all_nodes[1], file_handle)
    for node in all_nodes.values():
        file_handle.write(r'\node [blue, very thick] (%d) at (%d,%d,%d) {$\bullet$};' % (node.nid, node.x, node.y, node.z))
    file_handle.write(r'\end{tikzpicture}')


if __name__ == '__main__':
    for file in filter(lambda x: re.match('xmac_[a-z]+\.wiseml$', x), os.listdir('.')):
        print(file)
        all_nodes = {}
        xml.sax.parse(open(file), InstantiateNodesHandler())
        xml.sax.parse(open(file), BuildTreeHandler())
        #print_tree(all_nodes[1])
        with open('../report/%s.tex' % file, 'w') as tikz:
            export_tikz(tikz)
