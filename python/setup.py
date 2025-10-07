#!/usr/bin/env python3
"""
ZeroIPC - High-performance lock-free shared memory IPC with codata structures

This is a compatibility shim for older pip versions.
The actual configuration is in pyproject.toml.
"""

from setuptools import setup

# Read long description from README
with open("../README.md", "r", encoding="utf-8") as fh:
    long_description = fh.read()

setup(
    name="zeroipc",
    version="1.0.0",
    author="ZeroIPC Contributors",
    author_email="zeroipc@example.org",
    description="High-performance lock-free shared memory IPC with codata structures",
    long_description=long_description,
    long_description_content_type="text/markdown",
    url="https://github.com/[organization]/zeroipc",
    packages=["zeroipc"],
    classifiers=[
        "Development Status :: 5 - Production/Stable",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: MIT License",
        "Operating System :: POSIX :: Linux",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
        "Programming Language :: Python :: 3.12",
    ],
    python_requires=">=3.8",
    install_requires=[
        "numpy>=1.20.0",
        "psutil>=5.8.0",
    ],
)