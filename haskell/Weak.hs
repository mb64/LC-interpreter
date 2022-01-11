-- Weak normalization for the untyped lambda calculus.
--
-- The purpose of this benchmark is to compare my interpreter with GHC, the
-- highest-performance lazy evaluation runtime out there.
--
-- If one reimplemented all of GHC to be strongly-normalizing, this gives an
-- indication of how fast it could theoretically be.

{-# LANGUAGE BangPatterns, BlockArguments #-}

import Data.Coerce
import Unsafe.Coerce

newtype L = L (L -> L)

λ  = coerce :: (L -> L) -> L
λ2 = coerce :: (L -> L -> L) -> L
λ3 = coerce :: (L -> L -> L -> L) -> L
λ4 = coerce :: (L -> L -> L -> L -> L) -> L

ap  = coerce :: L -> L -> L
ap2 = coerce :: L -> L -> L -> L
ap3 = coerce :: L -> L -> L -> L -> L

n2 = λ2\ s z -> ap s (ap s z)
n3 = λ2\ s z -> ap s (ap s (ap s z))
n4 = λ2\ s z -> ap s (ap s (ap s (ap s z)))

{-# NOINLINE theTerm #-}
theTerm :: L
theTerm = ap2 minus bigNumber bigNumber

bigNumber = ap2 n4 n2 n3

minus = λ4\ n m s z ->
  ap3 n
    (λ2\ y k -> ap2 k (ap s (ap y (λ2\ a b -> a))) y)
    (λ\ k -> ap2 k z (λ\ _ -> z))
    (ap2 m (λ3\ k a b -> ap b k) (λ2\ a b -> a))


-- Really unsafe part =|
-- Converts Church numerals to ints, and other terms to UB
churchToInt :: L -> Int
churchToInt l = (unsafeCoerce l :: (Int -> Int) -> Int -> Int) succ 0

main = putStrLn $ "The church numerals represents " ++ show (churchToInt theTerm)
