from splat_renderer import render

if __name__ == '__main__':
    try:
        render("../models/scene0_anim2.ply", "../models/scene0_anim2.freiburg", "../output")
    except Exception as e:
        print(e)