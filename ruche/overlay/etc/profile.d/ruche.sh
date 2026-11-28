# Réglages Ruche pour les shells de connexion.
export PAGER="${PAGER:-more}"
if command -v nvim >/dev/null 2>&1; then
    export EDITOR=nvim
else
    export EDITOR=vi
fi
export PS1='\u@ruche:\w\$ '
alias ll='ls -la'
