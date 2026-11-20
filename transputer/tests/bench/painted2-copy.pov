// POV-Ray 1.0 scene exercising both RGB copy paths in painted2.
camera {
   location <0 1 -8>
   direction <0 0 1.2>
   up <0 1 0>
   right <1.333333 0 0>
   look_at <0 0 0>
}

object {
   sphere { <0 0 0> 2 }
   texture {
      painted2
      ambient 0.2
      diffuse 0.8
   }
}

object {
   light_source {
      <-10 7 -12>
      color red 1 green 1 blue 1
   }
}
