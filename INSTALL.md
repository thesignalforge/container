# Installation

## Docker (Recommended)

```bash
make docker-build   # Build image
make docker-test    # Run tests
make docker-shell   # Interactive shell
```

## Host Build

```bash
phpize
./configure --enable-signalforge-container
make
make test
sudo make install
```

Add to php.ini:
```ini
extension=signalforge_container.so
```

## Verify

```bash
php -m | grep signalforge_container
```

## Troubleshooting

**phpize not found:**
```bash
sudo apt install php-dev
```

**Debug build:**
```bash
./configure --enable-signalforge-container CFLAGS="-g -O0"
```

**Clean:**
```bash
make clean && phpize --clean
```
