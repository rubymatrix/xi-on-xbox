"""Shares a FINAL FANTASY XI install on the local network, for the Xbox app's "Copy game from a
computer" to copy it into the console's storage. Run it on a computer beside the Xbox (a Mac is fine).

  python3 tools/serve_game.py "<folder with FINAL FANTASY XI and PlayOnlineViewer>" [--port 8765]

It serves only what is in those two folders, read-only, while it runs:
  GET /manifest      one line per file: its size, a tab, its path (UTF-8, "/" between folders)
  GET /file/<path>   the file (a path from the manifest, URL-encoded); Range requests resume one
Stop it (Ctrl+C) when the copy is done. Standard library only.
"""
import argparse
import http.server
import os
import socket
import socketserver
import sys
import urllib.parse

FOLDERS = ('FINAL FANTASY XI', 'PlayOnlineViewer')


def build_manifest(root):
    files = {}
    for top in FOLDERS:
        for dirpath, dirnames, filenames in os.walk(os.path.join(root, top)):
            dirnames.sort()
            for name in sorted(filenames):
                if name.startswith('.'):  # .DS_Store and the like
                    continue
                full = os.path.join(dirpath, name)
                rel = os.path.relpath(full, root).replace(os.sep, '/')
                files[rel] = (full, os.path.getsize(full))
    return files


def local_addresses():
    addrs = set()
    try:  # the address used to reach the network
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(('192.0.2.1', 9))
        addrs.add(s.getsockname()[0])
        s.close()
    except OSError:
        pass
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            addrs.add(info[4][0])
    except OSError:
        pass
    return sorted(a for a in addrs if not a.startswith('127.'))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('root', help='the folder holding FINAL FANTASY XI and PlayOnlineViewer')
    ap.add_argument('--port', type=int, default=8765)
    ap.add_argument('--bind', default='0.0.0.0', help='the address to listen on (default: every one)')
    a = ap.parse_args()
    root = os.path.abspath(a.root)
    missing = [f for f in FOLDERS if not os.path.isdir(os.path.join(root, f))]
    if missing:
        sys.exit('%s has no %s folder' % (root, ' or '.join(missing)))

    print('listing %s ...' % root)
    files = build_manifest(root)
    total = sum(size for _, size in files.values())
    manifest = ''.join('%d\t%s\n' % (size, rel) for rel, (_, size) in sorted(files.items())).encode('utf-8')

    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = 'HTTP/1.1'  # keep-alive: tens of thousands of small files

        def log_message(self, fmt, *args):
            pass

        def send_bytes(self, status, data, ctype='text/plain; charset=utf-8'):
            self.send_response(status)
            self.send_header('Content-Type', ctype)
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            path = urllib.parse.urlsplit(self.path).path
            if path == '/manifest':
                return self.send_bytes(200, manifest)
            if not path.startswith('/file/'):
                return self.send_bytes(404, b'not found\n')
            entry = files.get(urllib.parse.unquote(path[len('/file/'):]))
            if entry is None:  # only what the manifest lists
                return self.send_bytes(404, b'not found\n')
            full, size = entry
            start = 0
            rng = self.headers.get('Range', '')
            if rng.startswith('bytes=') and rng[6:].split('-')[0].isdigit():
                start = min(int(rng[6:].split('-')[0]), size)
            self.send_response(206 if start else 200)
            self.send_header('Content-Type', 'application/octet-stream')
            self.send_header('Content-Length', str(size - start))
            if start:
                self.send_header('Content-Range', 'bytes %d-%d/%d' % (start, size - 1, size))
            self.end_headers()
            with open(full, 'rb') as f:
                f.seek(start)
                while True:
                    chunk = f.read(1 << 20)
                    if not chunk:
                        break
                    self.wfile.write(chunk)

    class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
        daemon_threads = True
        allow_reuse_address = True

        def handle_error(self, request, client_address):
            # the client closing a kept-alive connection, or cancelling a download, is not an error
            if isinstance(sys.exc_info()[1], (ConnectionResetError, BrokenPipeError, ConnectionAbortedError, TimeoutError)):
                return
            super().handle_error(request, client_address)

    server = Server((a.bind, a.port), Handler)
    print('sharing %d files, %.1f GB' % (len(files), total / 1e9))
    print('on the Xbox, in XI on Xbox > Copy game from a computer, enter one of:')
    for addr in local_addresses() or ['<this computer\'s address>']:
        print('    %s:%d' % (addr, a.port))
    print('Ctrl+C to stop')
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print('\nstopped')


if __name__ == '__main__':
    main()
