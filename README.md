# File Storage System

# ! DISCLAIMER !

This project is served as a proof-of-concept, not at anywhere had any intention
to deliver any production level stability, security, or even functionality.
The code has low quality, and has more focus on structure than performance.

## Introduction

Intro goes here

## Usage

How to use goes here

## Build

How to build goes here

## TODOs

Different TODOs goes here

 - [x] Basic Filesystem Structures
 - [x] Block Map to Memory
 - [x] Bitmap
 - [x] Ring Buffer
 - [x] Block Allocation and Deletion
 - [x] Block Attributes
 - [x] Block Copy-on-Write
 - [x] Inode
 - [x] Filesystem Basic Operations
 - [x] Filesystem Snapshot Operations
 - [x] FUSE Implementation
 - [x] Filesystem Reset (Snapshot Fast Recover)
 - [ ] Filesystem Crash Recovery (FCRM, Filesystem Check and Repair on Mount)
 - [ ] Robustness Test Results

## Used Public Repositories (Embedded in Source Files)
 * [CImg](https://github.com/GreycLab/CImg) Dependency for Terminal Image Viewer
 * [libpng](https://github.com/pnggroup/libpng) Dependency for Terminal Image Viewer
 * [Terminal Image Viewer](https://github.com/stefanhaustein/TerminalImageViewer.git) Display png files in the terminal
 * [LZ4](https://github.com/lz4/lz4) LZ4 compression
