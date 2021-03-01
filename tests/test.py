from splat_renderer import render

if __name__ == '__main__':
    try:
        render("../models/living-room_10000_noisy_10mm.ply", "../models/coords_flipped.txt", "../output", method="ewa",surfaceThickness=0.1, delta=100, pointSize=2e-2)
    except Exception as e:
        print(e)