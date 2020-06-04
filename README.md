# splat_renderer
Simple tool for rendering point clouds with known normals and point sizes. Only works with point clouds that have `position`, `normal`, `color`, and `radius` attributes. Trajectories have to be in [TUM Benchmark](https://vision.in.tum.de/data/datasets/rgbd-dataset/file_formats) format.

## Installation
```
git clone --recursive git@github.com:hesom/splat_renderer.git
cd splat_renderer
pip install .
```

## Example usage
```python
from splat_renderer import render

try:
  render('point_cloud.ply', 'trajectory.freiburg', 'output_dir')
except Exception as e:
  print(e)
```
