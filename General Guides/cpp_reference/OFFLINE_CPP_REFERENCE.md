# Offline C++ Reference

This repository includes an **offline C++ language and STL reference** inside the Docker image.

The offline docs are provided by Ubuntu's `cppreference-doc-en-html` package, which includes a local copy of the C and C++ standard library reference in HTML form.

## Quick start

From the repository root:

```bash
./serve-cpp-docs.sh
```

Then open:

```text
http://localhost:8080/en/index.html
```

This serves the documentation locally from your machine, so you do not need to browse the public web site every time.

## Terminal-only browsing

Open the Linux shell:

```bash
./linux-shell.sh
```

Then browse the docs in the terminal:

```bash
w3m /usr/share/cppreference/doc/html/en/index.html
```

## Notes

- The docs are available **offline after the Docker image is built**.
- They cover both **C++ language topics** and the **standard library/STL**.
- The packaged snapshot may not always match the very latest wording on the live web site, but it is ideal for local reference.
- Rebuild the image after Dockerfile changes:

```bash
docker build -t cpp-linux-dev .
```
