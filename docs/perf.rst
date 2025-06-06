Performance Tips
================

Intel GPUs
----------

#. Keep the system updated with the latest kernel and Mesa versions.
#. Ensure SoC firmware is up-to-date. These firmware updates currently
   require installing the Windows graphics driver; firmware updates
   via `fwupd` are in progress.
#. Use Wayland where possible, as it supports additional modifiers for
   better performance.
#. For MTL and newer integrated GPUs, disable VT-d if virtualization is
   not needed.
#. For discrete GPUs:

   #. Enable `ReBAR`_
   #. For workloads that keep the GPU busy (e.g. 3D videogames), 
      minimize idle power consumption by enabling `ASPM`_ powersave mode.
   #. For "bursty", latency-sensitive workloads (e.g. AI inference),
      enable ASPM performance mode.
   #. Enable the 'performance' `cpufreq`_ governor.

.. _ReBAR: https://www.intel.com/content/www/us/en/support/articles/000090831/graphics.html
.. _ASPM: https://docs.redhat.com/en/documentation/red_hat_enterprise_linux/6/html/power_management_guide/aspm
.. _cpufreq: https://docs.redhat.com/en/documentation/red_hat_enterprise_linux/6/html/performance_tuning_guide/s-cpu-cpufreq

Software rendering
------------------

#. Turn off smooth shading when you don't need it (glShadeModel)
#. Turn off depth buffering when you don't need it.
#. Turn off dithering when not needed.
#. Use double buffering as it's often faster than single buffering
#. Compile in the X Shared Memory extension option if it's supported on
   your system by adding -DSHM to CFLAGS and -lXext to XLIBS for your
   system in the Make-config file.
#. Recompile Mesa with more optimization if possible.
#. Try to maximize the amount of drawing done between glBegin/glEnd
   pairs.
#. Use the MESA_BACK_BUFFER variable to find best performance in double
   buffered mode. (X users only)
#. Optimized polygon rasterizers are employed when: rendering into back
   buffer which is an XImage RGB mode, not grayscale, not monochrome
   depth buffering is GL_LESS, or disabled flat or smooth shading
   dithered or non-dithered no other rasterization operations enabled
   (blending, stencil, etc)
#. Optimized line drawing is employed when: rendering into back buffer
   which is an XImage RGB mode, not grayscale, not monochrome depth
   buffering is GL_LESS or disabled flat shading dithered or
   non-dithered no other rasterization operations enabled (blending,
   stencil, etc)
#. Textured polygons are fastest when: using a 3-component (RGB), 2-D
   texture minification and magnification filters are GL_NEAREST texture
   coordinate wrap modes for S and T are GL_REPEAT GL_DECAL environment
   mode glHint( GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST ) depth
   buffering is GL_LESS or disabled
#. Lighting is fastest when: Two-sided lighting is disabled
   GL_LIGHT_MODEL_LOCAL_VIEWER is false GL_COLOR_MATERIAL is disabled No
   spot lights are used (all GL_SPOT_CUTOFFs are 180.0) No local lights
   are used (all position W's are 0.0) All material and light
   coefficients are >= zero
#. XFree86 users: if you want to use 24-bit color try starting your X
   server in 32-bit per pixel mode for better performance. That is,
   start your X server with startx -- -bpp 32 instead of startx -- -bpp
   24
#. Try disabling dithering with the MESA_NO_DITHER environment variable.
   If this environment variable is defined Mesa will disable dithering
   and the command glEnable(GL_DITHER) will be ignored.
