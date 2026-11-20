// POV-Ray 1.0 syntax, intentionally self-contained for the historical BTL.
camera {
  location <0 2 -6>
  right <1.3333 0 0>
  look_at <0 0 0>
}
object { light_source { <-5 8 -8> color red 1 green 1 blue 1 } }
object {
  sphere { <0 0 0> 1.3 }
  texture { color red 0.9 green 0.25 blue 0.12 ambient 0.2 diffuse 0.8 phong 0.7 phong_size 50 }
}
object {
  plane { <0 1 0> -1.3 }
  texture { color red 0.2 green 0.45 blue 0.7 ambient 0.2 diffuse 0.8 }
}
