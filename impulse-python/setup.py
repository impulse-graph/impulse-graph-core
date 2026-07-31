from setuptools import setup, find_packages

setup(
    name="impulse-graph",
    version="2.4.0",
    description="Impulse Graph Engine Python SDK & Zero-Copy C-ABI Binary Snapshot Binding",
    author="Impulse Graph Team",
    packages=find_packages(),
    python_requires=">=3.8",
    classifiers=[
        "Programming Language :: Python :: 3",
        "Programming Language :: C++",
        "License :: OSI Approved :: Apache Software License",
        "Operating System :: OS Independent",
    ],
)
