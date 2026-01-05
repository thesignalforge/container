# Signalforge Container Extension - Docker-based Build
IMAGE_NAME = signalforge-container

.PHONY: docker-build docker-test docker-example docker-shell docker-clean valgrind-test help

help:
	@echo "Signalforge Container Extension"
	@echo ""
	@echo "Usage:"
	@echo "  make docker-build   - Build Docker image with extension"
	@echo "  make docker-test    - Run tests in Docker"
	@echo "  make docker-example - Run example in Docker"
	@echo "  make docker-shell   - Interactive shell in Docker"
	@echo "  make docker-clean   - Remove Docker image"
	@echo "  make valgrind-test  - Run tests with Valgrind memory leak detection"

docker-build:
	docker build -t $(IMAGE_NAME) .

docker-test:
	docker run --rm -v $(PWD)/tests:/ext/tests $(IMAGE_NAME) php /opt/run-tests.php /ext/tests/

docker-example:
	docker run --rm -v $(PWD)/examples:/ext/examples $(IMAGE_NAME) php /ext/examples/basic.php

docker-shell:
	docker run --rm -it -v $(PWD):/ext $(IMAGE_NAME) sh

docker-clean:
	docker rmi $(IMAGE_NAME) 2>/dev/null || true

valgrind-test:
	@echo "Running tests with Valgrind memory leak detection..."
	@echo "This may take several minutes..."
	@valgrind --version > /dev/null 2>&1 || (echo "Error: Valgrind not installed. Install with: apt install valgrind (Linux) or brew install valgrind (macOS)" && exit 1)
	@valgrind --leak-check=full \
		--show-leak-kinds=all \
		--track-origins=yes \
		--verbose \
		--log-file=valgrind-output.txt \
		php /opt/run-tests.php tests/ || true
	@echo ""
	@echo "Valgrind output saved to: valgrind-output.txt"
	@echo ""
	@grep -A 5 "LEAK SUMMARY" valgrind-output.txt || echo "No leaks detected!"
	@grep "ERROR SUMMARY" valgrind-output.txt || echo "No errors detected!"
