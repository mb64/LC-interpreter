-- Strong normalization for the untyped lambda calculus, using a HOAS deep
-- embedding
--
-- The purpose of this benchmark is to compare my interpreter with something
-- implemented by compiling to Haskell.
--
-- Computes the same thing as bench.lc

{-# LANGUAGE BangPatterns, BlockArguments #-}

type Var = Int

data Val = Fun !(Val -> Val) | VVar !Var | VApp !Val Val
data NF = Lam NF | NE Var [NF] deriving (Show, Eq)

infixl 1 $$
($$) :: Val -> Val -> Val
Fun f $$ x = f x
v     $$ x = VApp v x
{-# INLINE ($$) #-}

λ = Fun

n2 = λ\s -> λ\z -> s $$ (s $$ z)
n3 = λ\s -> λ\z -> s $$ (s $$ (s $$ z))
n4 = λ\s -> λ\z -> s $$ (s $$ (s $$ (s $$ z)))

theTerm = minus $$ bigNumber $$ bigNumber
bigNumber = n4 $$ n2 $$ n3
minus = λ\n -> λ\m -> λ\s -> λ\z ->
  n $$ (λ\y -> λ\k -> k $$ (s $$ (y $$ λ\a -> λ\b -> a)) $$ y)
    $$ (λ\k -> k $$ z $$ (λ\_ -> z))
    $$ (m $$ (λ\k -> λ\a -> λ\b -> b $$ k) $$ (λ\a -> λ\b -> a))

readback :: Var -> Val -> NF
readback !lvl (Fun f) = Lam $ readback (lvl+1) $ f (VVar lvl)
readback !lvl (VVar v) = NE v []
readback !lvl (VApp f x) = 
  let NE v sp = readback lvl f in NE v (readback lvl x:sp)

main = do
  putStrLn "Normalizing the term..."
  print $ readback 0 theTerm

