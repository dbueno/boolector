(benchmark smtaxiombvashr
 :logic QF_BV
 :extrafuns ((s BitVec[32]))
 :extrafuns ((t BitVec[32]))
 :formula (not (=
    (bvashr s t)  
      (ite (= (extract[31:31] s) bit0)
           (bvlshr s t)
           (bvnot (bvlshr (bvnot s) t)))
)))
