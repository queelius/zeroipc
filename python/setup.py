"""
Setup script for posix_shm Python bindings
"""

from setuptools import setup, Extension, find_packages
from pybind11.setup_helpers import Pybind11Extension, build_ext

__version__ = "1.0.0"

ext_modules = [
    Pybind11Extension(
        "posix_shm_py",
        ["src/python_bindings.cpp"],
        include_dirs=["../include"],
        cxx_std=23,
        extra_compile_args=["-O3", "-march=native", "-fPIC"],
        libraries=["rt", "pthread"],
        define_macros=[("VERSION_INFO", __version__)],
    ),
]

with open("../README.md", "r", encoding="utf-8") as fh:
    long_description = fh.read()

setup(
    name="posix-shm",
    version=__version__,
    author="Alex Towell",
    author_email="",
    url="https://github.com/queelius/posix_shm",
    description="High-performance lock-free data structures in POSIX shared memory for Python",
    long_description=long_description,
    long_description_content_type="text/markdown",
    ext_modules=ext_modules,
    cmdclass={"build_ext": build_ext},
    packages=find_packages(),
    python_requires=">=3.8",
    install_requires=[],
    extras_require={
        "dev": [
            "pytest>=7.0",
            "numpy>=1.20",
            "pybind11>=2.10",
        ]
    },
    classifiers=[
        "Development Status :: 4 - Beta",
        "Intended Audience :: Developers",
        "Intended Audience :: Science/Research",
        "License :: OSI Approved :: MIT License",
        "Operating System :: POSIX :: Linux",
        "Programming Language :: C++",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
        "Programming Language :: Python :: 3.12",
        "Topic :: Software Development :: Libraries",
        "Topic :: System :: Hardware :: Symmetric Multi-processing",
        "Topic :: Scientific/Engineering",
    ],
    keywords="shared-memory ipc lock-free data-structures multiprocessing",
    project_urls={
        "Bug Reports": "https://github.com/queelius/posix_shm/issues",
        "Documentation": "https://queelius.github.io/posix_shm/",
        "Source": "https://github.com/queelius/posix_shm",
    },
)