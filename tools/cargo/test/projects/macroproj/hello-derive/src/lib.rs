//! #[derive(Hello)] : une méthode hello() qui rend le nom du type et le nombre de ses champs
use proc_macro::{TokenStream, TokenTree};

#[proc_macro_derive(Hello)]
pub fn derive_hello(input: TokenStream) -> TokenStream {
    let mut it = input.into_iter();
    let mut name = String::new();
    let mut fields = 0;
    while let Some(t) = it.next() {
        if let TokenTree::Ident(i) = &t {
            if i.to_string() == "struct" {
                if let Some(TokenTree::Ident(n)) = it.next() {
                    name = n.to_string();
                }
            }
        }
        if let TokenTree::Group(g) = &t {
            fields = g.stream().into_iter().filter(|t| matches!(t, TokenTree::Punct(p) if p.as_char() == ':')).count();
        }
    }
    format!(
        "impl {name} {{ pub fn hello(&self) -> String {{ format!(\"{name} : {fields} champ(s), macro chargée par rustc dans Korin\") }} }}"
    )
    .parse()
    .unwrap()
}
